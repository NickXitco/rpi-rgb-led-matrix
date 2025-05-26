#include "led-matrix.h"
#include "graphics.h"
#include "animation.h"

#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <Magick++.h>
#include <chrono>

using namespace rgb_matrix;
using json = nlohmann::json;
using namespace std::chrono;

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
  interrupt_received = true;
}

// Callback function for CURL to write response data
struct WriteCallback {
    std::string data;
    static size_t Write(void* contents, size_t size, size_t nmemb, std::string* userp) {
        userp->append((char*)contents, size * nmemb);
        return size * nmemb;
    }
};

// Function to fetch weather data
json GetWeatherData(const std::string& api_key) {
    CURL* curl = curl_easy_init();
    if (!curl) return json::object();

    std::string url = "https://api.openweathermap.org/data/2.5/weather?q=New%20York&units=imperial&appid=" + api_key;
    WriteCallback callback;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback::Write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &callback.data);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) return json::object();

    try {
        return json::parse(callback.data);
    } catch (...) {
        return json::object();
    }
}

// Function to fetch and draw weather icon
void DrawWeatherIcon(FrameCanvas* canvas, const std::string& icon_code, int x, int y) {
    CURL* curl = curl_easy_init();
    if (!curl) return;

    std::string url = "https://openweathermap.org/img/wn/" + icon_code + "@2x.png";
    WriteCallback callback;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback::Write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &callback.data);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) return;

    try {
        // Create a temporary file to store the image
        std::string temp_file = "/tmp/weather_icon.png";
        std::ofstream out(temp_file, std::ios::binary);
        out.write(callback.data.c_str(), callback.data.size());
        out.close();

        // Load and process the image with ImageMagick
        Magick::Image image;
        image.read(temp_file);
        
        // Resize to fit our display (32x32 pixels)
        image.resize(Magick::Geometry(32, 32));
        
        // Convert to RGB and draw on canvas
        for (size_t i = 0; i < image.rows(); i++) {
            for (size_t j = 0; j < image.columns(); j++) {
                Magick::ColorRGB color = image.pixelColor(j, i);
                canvas->SetPixel(x + j, y + i,
                               static_cast<uint8_t>(color.red() * 255),
                               static_cast<uint8_t>(color.green() * 255),
                               static_cast<uint8_t>(color.blue() * 255));
            }
        }

        // Clean up temporary file
        std::remove(temp_file.c_str());
    } catch (const std::exception& e) {
        fprintf(stderr, "Error processing weather icon: %s\n", e.what());
    }
}

int main(int argc, char *argv[]) {
    // Initialize ImageMagick
    Magick::InitializeMagick(*argv);

    // Read API key from file
    std::ifstream key_file("openweathermap_api_key.txt");
    if (!key_file.is_open()) {
        fprintf(stderr, "Please create a file named 'openweathermap_api_key.txt' with your OpenWeatherMap API key\n");
        return 1;
    }
    std::string api_key;
    std::getline(key_file, api_key);
    key_file.close();

    RGBMatrix::Options defaults;
    defaults.hardware_mapping = "regular";
    defaults.rows = 32;
    defaults.cols = 64;
    defaults.chain_length = 1;
    defaults.parallel = 1;
    defaults.show_refresh_rate = true;

    signal(SIGTERM, InterruptHandler);
    signal(SIGINT, InterruptHandler);

    RGBMatrix *matrix = RGBMatrix::CreateFromFlags(&argc, &argv, &defaults);
    if (matrix == NULL)
        return 1;

    FrameCanvas *offscreen = matrix->CreateFrameCanvas();

    rgb_matrix::Font font;
    if (!font.LoadFont("../fonts/8x13B.bdf")) {
        fprintf(stderr, "Couldn't load font\n");
        return 1;
    }

    Color temp_color(178, 216, 206);

    // Create background animation
    PerlinNoiseAnimation background(offscreen);
    auto last_frame = high_resolution_clock::now();
    // The first loop should get the weather data
    auto last_weather_update = last_frame + seconds(500);

    while (!interrupt_received) {
        auto now = high_resolution_clock::now();
        float delta_time = duration<float>(now - last_frame).count();
        last_frame = now;

        // Update background animation
        background.Update(delta_time);
        background.Draw();

        // Update weather data every 5 minutes
        if (duration<float>(now - last_weather_update).count() >= 300.0f) {
            json weather_data = GetWeatherData(api_key);
            
            if (!weather_data.empty()) {
                try {
                    // Get temperature and weather icon
                    double temp = weather_data["main"]["temp"].get<double>();
                    std::string icon = weather_data["weather"][0]["icon"].get<std::string>();
                    
                    // Draw weather icon
                    DrawWeatherIcon(offscreen, icon, 0, 0);
                    
                    // Draw temperature
                    std::string temp_str = std::to_string(static_cast<int>(round(temp))) + "°F";
                    rgb_matrix::DrawText(offscreen, font, 29, 23, temp_color, temp_str.c_str());
                } catch (const std::exception& e) {
                    fprintf(stderr, "Error processing weather data: %s\n", e.what());
                    rgb_matrix::DrawText(offscreen, font, 2, 15, temp_color, "Error");
                }
            } else {
                rgb_matrix::DrawText(offscreen, font, 2, 15, temp_color, "No Data");
            }
            last_weather_update = now;
        }

        offscreen = matrix->SwapOnVSync(offscreen);
    }

    delete matrix;
    return 0;
} 