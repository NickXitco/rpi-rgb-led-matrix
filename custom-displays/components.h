#pragma once

#include "visual-system.h"
#include "graphics.h"
#include "led-matrix.h"
#include <random>
#include <mutex>
#include <string>
#include <vector>
#include <atomic>
#include <future>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

// Include STB Perlin noise (header only)
#include "external/stb_perlin.h"

using json = nlohmann::json;

// Perlin Noise Background Component
class PerlinNoiseBackground : public Background
{
public:
    PerlinNoiseBackground(const std::string &name = "perlin_noise",
                          const VisualColor &color = VisualColor(50, 25, 255));

    void Update(float delta_time) override;
    void Draw(Canvas *canvas) override;
    void SetColor(const VisualColor &color) override;
    void SetParameter(const std::string &param, float value) override;

private:
    VisualColor color_;
    float z_;
    float scale_;
    float speed_;
    int seed_;
    std::mt19937 rng_;
};

// Weather Icon structure
struct WeatherIcon
{
    std::vector<uint8_t> pixels; // RGBA data
    int width;
    int height;
    std::string icon_code;

    WeatherIcon() : width(0), height(0) {}
};

// Weather Overlay Component
class WeatherOverlay : public Overlay
{
public:
    WeatherOverlay(const std::string &name = "weather",
                   const std::string &api_key = "");

    void Initialize() override;
    void Update(float delta_time) override;
    void Draw(Canvas *canvas) override;
    void Cleanup() override;

    // Configuration
    void SetApiKey(const std::string &api_key) { api_key_ = api_key; }
    void SetLocation(double lat, double lon)
    {
        lat_ = lat;
        lon_ = lon;
    }
    void SetUpdateInterval(float seconds) { update_interval_ = seconds; }
    void SetTemperatureColor(const VisualColor &color) { temp_color_ = color; }

private:
    std::string api_key_;
    double lat_;
    double lon_;
    float update_interval_;
    float time_since_update_;
    VisualColor temp_color_;

    // Weather data
    struct WeatherData
    {
        double temperature;
        std::string icon_code;
        WeatherIcon icon;
        bool has_data;
        std::mutex mutex;

        WeatherData() : temperature(0.0), has_data(false) {}
    } weather_data_;

    // Font
    rgb_matrix::Font font_;

    // Helper methods
    json FetchWeatherData();
    WeatherIcon FetchWeatherIcon(const std::string &icon_code);
    void DrawWeatherIcon(Canvas *canvas, const WeatherIcon &icon, int x, int y);

    // CURL callback
    static size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *userp);
};

// Text Overlay Component
class TextOverlay : public Overlay
{
public:
    TextOverlay(const std::string &name, const std::string &text = "",
                const VisualColor &color = VisualColor(255, 255, 255));

    void Initialize() override;
    void Update(float delta_time) override;
    void Draw(Canvas *canvas) override;

    // Configuration
    virtual void SetText(const std::string &text) { text_ = text; }
    void SetTextColor(const VisualColor &color) { text_color_ = color; }
    void SetFontFile(const std::string &font_file) { font_file_ = font_file; }

protected:
    std::string text_;
    VisualColor text_color_;
    std::string font_file_;
    rgb_matrix::Font font_;
    bool font_loaded_;
};

// Marquee Text Overlay Component
class MarqueeTextOverlay : public TextOverlay
{
public:
    MarqueeTextOverlay(const std::string &name, const std::string &text = "",
                       const VisualColor &color = VisualColor(255, 255, 255),
                       int max_display_width = 50, int character_width = 4);

    void Initialize() override;
    void Update(float delta_time) override;
    void Draw(Canvas *canvas) override;

    // Configuration
    void SetMaxDisplayWidth(int width) { max_display_width_ = width; }
    void SetScrollSpeed(float pixels_per_second) { scroll_speed_ = pixels_per_second; }
    void SetPauseDuration(float seconds) { pause_duration_ = seconds; }
    void SetCharacterWidth(int width) { character_width_ = width; }
    void SetText(const std::string &text) override;

private:
    int max_display_width_; // Maximum width in pixels to display
    int character_width_;   // Width of each character in pixels
    float scroll_speed_;    // Pixels per second to scroll
    float pause_duration_;  // Seconds to pause at beginning
    float scroll_offset_;   // Current scroll offset in pixels
    float pause_timer_;     // Current pause timer
    int text_width_;        // Width of rendered text in pixels
    bool is_scrolling_;     // Whether currently scrolling
    bool needs_scrolling_;  // Whether text is long enough to need scrolling
    bool scroll_direction_; // true = right-to-left, false = left-to-right

    // Off-screen buffer for text rendering
    std::vector<uint8_t> text_buffer_;
    int buffer_width_;
    int buffer_height_;

    void RenderTextToBuffer();
    void ResetScrolling();
    int CalculateTextWidth(const std::string &text);
    void DrawScrollingText(Canvas *canvas, const Color &color);
    int DrawClippedGlyph(Canvas *canvas, int x_pos, int y_pos, const Color &color,
                         char glyph, int clip_left, int clip_right);
};

// Spotify Album Art structure
struct SpotifyAlbumArt
{
    std::vector<uint8_t> pixels; // RGBA data
    int width;
    int height;
    std::string url;

    SpotifyAlbumArt() : width(0), height(0) {}
};

// Spotify Overlay Component
class SpotifyOverlay : public Overlay
{
public:
    SpotifyOverlay(const std::string &name = "spotify",
                   const std::string &client_id = "",
                   const std::string &client_secret = "",
                   const std::string &refresh_token = "");

    void Initialize() override;
    void Update(float delta_time) override;
    void Draw(Canvas *canvas) override;
    void Cleanup() override;

    // Configuration
    void SetCredentials(const std::string &client_id, const std::string &client_secret, const std::string &refresh_token);
    void SetPollingInterval(float seconds) { polling_interval_ = seconds; }
    void SetTextColor(const VisualColor &color) { text_color_ = color; }

private:
    std::string client_id_;
    std::string client_secret_;
    std::string refresh_token_;
    std::string access_token_;
    float polling_interval_;
    float time_since_poll_;
    float time_since_token_refresh_;
    VisualColor text_color_;

    // Threading for non-blocking API calls
    std::atomic<bool> api_call_in_progress_;
    std::future<void> api_future_;

    // Current track data
    struct CurrentTrack
    {
        std::string track_name;
        std::string artist_name;
        std::string album_art_url;
        SpotifyAlbumArt album_art;
        bool is_playing;
        bool has_data;
        int progress_ms;
        int duration_ms;
        std::mutex mutex;

        CurrentTrack() : is_playing(false), has_data(false), progress_ms(0), duration_ms(0) {}
    } current_track_;

    // Font
    rgb_matrix::Font font_;

    // Marquee text overlays for scrolling long track/artist names
    std::unique_ptr<MarqueeTextOverlay> track_marquee_;
    std::unique_ptr<MarqueeTextOverlay> artist_marquee_;
    std::string last_track_name_;
    std::string last_artist_name_;

    // Helper methods
    std::string RefreshAccessToken();
    json FetchCurrentlyPlaying();
    SpotifyAlbumArt FetchAlbumArt(const std::string &image_url);
    void DrawAlbumArt(Canvas *canvas, const SpotifyAlbumArt &art, int x, int y);
    void DrawProgressBar(Canvas *canvas, int x, int y, int width, int height, float progress);
    std::string Base64Encode(const std::string &input);

    // CURL callback
    static size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *userp);
};