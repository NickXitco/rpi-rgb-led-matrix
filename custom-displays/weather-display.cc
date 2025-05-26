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
#include <mutex>
#include <vector>

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

    std::string url = "https://api.openweathermap.org/data/3.0/onecall?lat=40.747435&lon=-73.993702&units=imperial&exclude=minutely%2Chourly%2Cdaily&appid=" + api_key;
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

// Structure to hold weather icon data
struct WeatherIcon {
    std::vector<uint8_t> pixels;  // RGBA data
    int width;
    int height;
    std::string icon_code;

    WeatherIcon() : width(0), height(0) {}
};

// Function to fetch weather icon
WeatherIcon FetchWeatherIcon(const std::string& icon_code) {
    WeatherIcon icon;
    icon.icon_code = icon_code;

    CURL* curl = curl_easy_init();
    if (!curl) return icon;

    std::string url = "https://openweathermap.org/img/wn/" + icon_code + "@2x.png";
    WriteCallback callback;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback::Write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &callback.data);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) return icon;

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
        
        // Store the image data
        icon.width = image.columns();
        icon.height = image.rows();
        icon.pixels.resize(icon.width * icon.height * 4);  // RGBA format

        // Convert to RGBA and store in pixels vector
        for (size_t i = 0; i < image.rows(); i++) {
            for (size_t j = 0; j < image.columns(); j++) {
                Magick::ColorRGB color = image.pixelColor(j, i);
                size_t idx = (i * icon.width + j) * 4;
                icon.pixels[idx] = static_cast<uint8_t>(color.red() * 255);
                icon.pixels[idx + 1] = static_cast<uint8_t>(color.green() * 255);
                icon.pixels[idx + 2] = static_cast<uint8_t>(color.blue() * 255);
                icon.pixels[idx + 3] = 255;  // Alpha
            }
        }

        // Clean up temporary file
        std::remove(temp_file.c_str());
    } catch (const std::exception& e) {
        fprintf(stderr, "Error processing weather icon: %s\n", e.what());
    }

    return icon;
}

// Function to draw weather icon from cached data
void DrawWeatherIcon(FrameCanvas* canvas, const WeatherIcon& icon, int x, int y) {
    if (icon.pixels.empty()) return;

    for (int i = 0; i < icon.height; i++) {
        for (int j = 0; j < icon.width; j++) {
            size_t idx = (i * icon.width + j) * 4;
            canvas->SetPixel(x + j, y + i,
                           icon.pixels[idx],
                           icon.pixels[idx + 1],
                           icon.pixels[idx + 2]);
        }
    }
}

// Structure to hold weather data
struct WeatherData {
    double temperature;
    std::string icon_code;
    WeatherIcon icon;
    bool has_data;
    std::mutex mutex;

    WeatherData() : temperature(0.0), has_data(false) {}
};

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

    Color temp_color(178, 226, 206);

    // Create background animation
    PerlinNoiseAnimation background(offscreen);
    auto last_frame = high_resolution_clock::now();
    auto last_weather_update = last_frame;
    
    // Weather data storage
    WeatherData weather_data;

    // Initial weather data fetch
    json initial_data = GetWeatherData(api_key);
    if (!initial_data.empty()) {
        try {
            std::lock_guard<std::mutex> lock(weather_data.mutex);
            weather_data.temperature = initial_data["current"]["temp"].get<double>();
            weather_data.icon_code = initial_data["current"]["weather"][0]["icon"].get<std::string>();
            weather_data.icon = FetchWeatherIcon(weather_data.icon_code);
            weather_data.has_data = true;
        } catch (const std::exception& e) {
            fprintf(stderr, "Error processing initial weather data: %s\n", e.what());
        }
    }

    while (!interrupt_received) {
        auto now = high_resolution_clock::now();
        float delta_time = duration<float>(now - last_frame).count();
        last_frame = now;

        // Update background animation
        background.Update(delta_time);
        background.Draw();

        // Draw current weather data
        {
            std::lock_guard<std::mutex> lock(weather_data.mutex);
            if (weather_data.has_data) {
                // Draw weather icon
                DrawWeatherIcon(offscreen, weather_data.icon, 0, 0);
                
                // Draw temperature
                std::string temp_str = std::to_string(static_cast<int>(round(weather_data.temperature))) + "°F";
                fprintf(stderr, "Drawing temperature: %s\n", temp_str.c_str());
                rgb_matrix::DrawText(offscreen, font, 31, 22, temp_color, temp_str.c_str());
            } else {
                rgb_matrix::DrawText(offscreen, font, 2, 15, temp_color, "No Data");
            }
        }

        // Update weather data every 5 minutes
        if (duration<float>(now - last_weather_update).count() >= 300.0f) {
            json new_data = GetWeatherData(api_key);
            if (!new_data.empty()) {
                try {
                    // Create new weather data instance
                    WeatherData new_weather;
                    new_weather.temperature = new_data["current"]["temp"].get<double>();
                    std::string new_icon_code = new_data["current"]["weather"][0]["icon"].get<std::string>();
                    
                    // Only fetch new icon if the code has changed
                    if (new_icon_code != weather_data.icon_code) {
                        new_weather.icon_code = new_icon_code;
                        new_weather.icon = FetchWeatherIcon(new_weather.icon_code);
                    } else {
                        // Copy existing icon data
                        new_weather.icon_code = weather_data.icon_code;
                        new_weather.icon = weather_data.icon;
                    }
                    
                    new_weather.has_data = true;

                    // Atomic swap of weather data
                    {
                        std::lock_guard<std::mutex> lock(weather_data.mutex);
                        weather_data = std::move(new_weather);
                    }
                     
                    fprintf(stderr, "Updated temperature to %.1f°F\n", weather_data.temperature);
                } catch (const std::exception& e) {
                    fprintf(stderr, "Error processing weather data: %s\n", e.what());
                }
            }
            last_weather_update = now;
        }

        offscreen = matrix->SwapOnVSync(offscreen);
    }

    delete matrix;
    return 0;
} 