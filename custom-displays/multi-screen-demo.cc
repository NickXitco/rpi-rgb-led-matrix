#include "visual-system.h"
#include "components.h"
#include <signal.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <cstdlib>

// ImageMagick is available (based on Makefile configuration)
#define MAGICK_AVAILABLE
#include <Magick++.h>

using namespace rgb_matrix;
using namespace std::chrono;

volatile bool interrupt_received = false;
static void InterruptHandler(int signo)
{
    interrupt_received = true;
}

// Helper function to safely read environment variables
std::string GetEnvVar(const std::string &var_name, const std::string &default_value = "")
{
    const char *value = std::getenv(var_name.c_str());
    return value ? std::string(value) : default_value;
}

// Screen Factory Functions (using shared background)
std::shared_ptr<Screen> CreatePerlinScreen(std::shared_ptr<PerlinNoiseBackground> shared_background)
{
    auto screen = std::make_shared<Screen>("perlin");
    screen->AddBackground(shared_background);
    return screen;
}

std::shared_ptr<Screen> CreateTextDemoScreen(std::shared_ptr<PerlinNoiseBackground> shared_background)
{
    auto screen = std::make_shared<Screen>("text_demo");
    screen->AddBackground(shared_background);

    // Add text overlays
    auto title = std::make_shared<TextOverlay>("title", "HELLO",
                                               VisualColor(255, 255, 255));
    title->SetPosition(2, 12);
    screen->AddOverlay(title);

    auto subtitle = std::make_shared<TextOverlay>("subtitle", "WORLD",
                                                  VisualColor(255, 200, 100));
    subtitle->SetPosition(2, 24);
    screen->AddOverlay(subtitle);

    return screen;
}

std::shared_ptr<Screen> CreateWeatherScreen(std::shared_ptr<PerlinNoiseBackground> shared_background, const std::string &api_key)
{
    auto screen = std::make_shared<Screen>("weather");
    screen->AddBackground(shared_background);

    // Weather overlay
    auto weather_overlay = std::make_shared<WeatherOverlay>("weather_info", api_key);
    weather_overlay->SetPosition(0, 0);
    weather_overlay->SetLocation(40.747435, -73.993702); // NYC coordinates
    weather_overlay->SetUpdateInterval(300.0f);          // 5 minutes
    weather_overlay->SetTemperatureColor(VisualColor(178, 226, 206));

    // Initialize immediately to start background weather fetching
    weather_overlay->Initialize();

    screen->AddOverlay(weather_overlay);

    return screen;
}

std::shared_ptr<Screen> CreateDualColorScreen(std::shared_ptr<PerlinNoiseBackground> shared_background)
{
    auto screen = std::make_shared<Screen>("dual_color");
    screen->AddBackground(shared_background);

    // Text overlay
    auto text = std::make_shared<TextOverlay>("dual_text", "DUAL",
                                              VisualColor(255, 255, 255));
    text->SetPosition(10, 16);
    screen->AddOverlay(text);

    return screen;
}

std::shared_ptr<Screen> CreateSpotifyScreen(std::shared_ptr<PerlinNoiseBackground> shared_background)
{
    auto screen = std::make_shared<Screen>("spotify");
    screen->AddBackground(shared_background);

    // Read Spotify credentials from environment variables
    std::string client_id = GetEnvVar("SPOTIFY_CLIENT_ID");
    std::string client_secret = GetEnvVar("SPOTIFY_CLIENT_SECRET");
    std::string refresh_token = GetEnvVar("SPOTIFY_REFRESH_TOKEN");

    if (client_id.empty() || client_secret.empty() || refresh_token.empty())
    {
        std::cerr << "Warning: Spotify credentials not found in environment variables.\n";
        std::cerr << "Please set: SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET, SPOTIFY_REFRESH_TOKEN\n";
    }

    // Add Spotify overlay with credentials from environment
    auto spotify = std::make_shared<SpotifyOverlay>(
        "spotify",
        client_id,
        client_secret,
        refresh_token);
    spotify->SetPosition(2, 2);
    spotify->SetPollingInterval(2.0f);
    spotify->SetTextColor(VisualColor(255, 255, 255));
    screen->AddOverlay(spotify);

    return screen;
}

void RunScreenCycling(DisplayManager &display_manager, const std::vector<std::string> &screen_names,
                      std::shared_ptr<PerlinNoiseBackground> shared_background)
{
    // Define colors for each screen
    std::vector<VisualColor> screen_colors = {
        VisualColor(50, 25, 255),   // perlin - blue
        VisualColor(100, 50, 100),  // text_demo - purple
        VisualColor(178, 226, 206), // weather - teal
        VisualColor(255, 100, 100), // dual_color - red
        VisualColor(128, 128, 128)  // spotify - light grey
    };

    // Start with the first screen
    display_manager.SetActiveScreen(screen_names[0]);
    shared_background->SetColor(screen_colors[0]);

    std::cout << "Starting multi-screen demo. Cycling through " << screen_names.size() << " screens.\n";
    std::cout << "Screens: ";
    for (size_t i = 0; i < screen_names.size(); i++)
    {
        std::cout << screen_names[i];
        if (i < screen_names.size() - 1)
            std::cout << " -> ";
    }
    std::cout << " (repeating)\n";
    std::cout << "Each screen displays for 120 seconds. Press Ctrl+C to exit.\n";

    // Run screen switching in a separate thread

    const int screen_duration_seconds = 20;
    std::thread screen_switcher([&]()
                                {
        size_t current_screen = 0;
        while (!interrupt_received) {
            // Sleep in small chunks so we can respond to interrupts quickly
            for (int i = 0; i < screen_duration_seconds * 10 && !interrupt_received; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            if (!interrupt_received) {
                current_screen = (current_screen + 1) % screen_names.size();
                std::cout << "Switching to screen: " << screen_names[current_screen] << std::endl;
                
                // Change the background color smoothly
                shared_background->SetColor(screen_colors[current_screen]);
                display_manager.SetActiveScreen(screen_names[current_screen]);
            }
        } });

    // Run the main display loop
    display_manager.Run();

    // Wait for the switcher thread to finish
    screen_switcher.join();
}

int main(int argc, char *argv[])
{
    // Initialize ImageMagick
    Magick::InitializeMagick(*argv);

    // Setup matrix configuration
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
    {
        return 1;
    }

    // Create the display manager
    DisplayManager display_manager(matrix);

    // Create shared Perlin noise background (starts with blue)
    auto shared_background = std::make_shared<PerlinNoiseBackground>("shared_perlin_bg",
                                                                     VisualColor(50, 25, 255));
    shared_background->SetParameter("scale", 0.1f);
    shared_background->SetParameter("speed", 0.3f);

    // Create all screens using factory functions with shared background
    std::string weather_api_key = GetEnvVar("WEATHER_API_KEY");
    if (weather_api_key.empty())
    {
        std::cerr << "Warning: Weather API key not found. Please set WEATHER_API_KEY environment variable.\n";
    }

    auto perlin_screen = CreatePerlinScreen(shared_background);
    auto text_screen = CreateTextDemoScreen(shared_background);
    auto weather_screen = CreateWeatherScreen(shared_background, weather_api_key);
    auto dual_screen = CreateDualColorScreen(shared_background);
    auto spotify_screen = CreateSpotifyScreen(shared_background);

    // Add screens to display manager
    // display_manager.AddScreen(perlin_screen);
    // display_manager.AddScreen(text_screen);
    display_manager.AddScreen(spotify_screen);
    display_manager.AddScreen(weather_screen);
    // display_manager.AddScreen(dual_screen);

    // Set up screen cycling and run the display
    std::vector<std::string> screen_names = {"spotify", "weather"};
    RunScreenCycling(display_manager, screen_names, shared_background);

    delete matrix;
    return 0;
}