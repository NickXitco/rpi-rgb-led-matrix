#include "components.h"
#include <fstream>
#include <sstream>
#include <cmath>
#include <iostream>
#include <mutex>
#include <future>
#include <atomic>
#include <chrono>
#include <curl/curl.h>

// Include STB Perlin implementation (only in this file)
#define STB_PERLIN_IMPLEMENTATION
#include "external/stb_perlin.h"

// ImageMagick is available (based on Makefile configuration)
#define MAGICK_AVAILABLE
#include <Magick++.h>

// PerlinNoiseBackground Implementation
PerlinNoiseBackground::PerlinNoiseBackground(const std::string &name, const VisualColor &color)
    : Background(name), color_(color), z_(0.0f), scale_(0.1f), speed_(0.5f)
{
    std::random_device rd;
    seed_ = rd();
    rng_.seed(seed_);
}

void PerlinNoiseBackground::Update(float delta_time)
{
    z_ += speed_ * delta_time;
}

void PerlinNoiseBackground::Draw(Canvas *canvas)
{
    for (int x = 0; x < canvas->width(); ++x)
    {
        for (int y = 0; y < canvas->height(); ++y)
        {
            float nx = x * scale_;
            float ny = y * scale_;
            float nz = z_;

            float n = (stb_perlin_noise3(nx, ny, nz, 0, 0, 0) + 1.0f) * 0.5f;
            float eased = (n * n * n);
            uint8_t value = static_cast<uint8_t>(eased * 120);

            canvas->SetPixel(x, y,
                             value * (color_.r / 255.0f),
                             value * (color_.g / 255.0f),
                             value * (color_.b / 255.0f));
        }
    }
}

void PerlinNoiseBackground::SetColor(const VisualColor &color)
{
    color_ = color;
}

void PerlinNoiseBackground::SetParameter(const std::string &param, float value)
{
    if (param == "scale")
    {
        scale_ = value;
    }
    else if (param == "speed")
    {
        speed_ = value;
    }
}

// WeatherOverlay Implementation
WeatherOverlay::WeatherOverlay(const std::string &name, const std::string &api_key)
    : Overlay(name), api_key_(api_key), lat_(40.747435), lon_(-73.993702),
      update_interval_(300.0f), temp_color_(178, 226, 206), background_thread_running_(false), initialized_(false)
{
}

WeatherOverlay::~WeatherOverlay()
{
    // Stop background thread in destructor to ensure proper cleanup
    if (background_thread_running_.load())
    {
        background_thread_running_.store(false);
        if (background_thread_.joinable())
        {
            background_thread_.join();
        }
    }
    initialized_.store(false);
}

void WeatherOverlay::Initialize()
{
    // Prevent double initialization
    if (initialized_.load())
    {
        return;
    }

    // Load font
    if (!font_.LoadFont("../fonts/8x13B.bdf"))
    {
        fprintf(stderr, "Warning: Couldn't load font for weather overlay\n");
    }

    // Start background thread for weather updates
    if (!api_key_.empty())
    {
        background_thread_running_.store(true);
        background_thread_ = std::thread(&WeatherOverlay::BackgroundWeatherUpdate, this);
        fprintf(stderr, "Started background weather updates (interval: %.0f seconds)\n", update_interval_);
    }

    initialized_.store(true);
}

void WeatherOverlay::Update(float delta_time)
{
    // Weather data is now updated in the background thread
    // This method is kept for compatibility but doesn't need to do anything
}

void WeatherOverlay::Draw(Canvas *canvas)
{
    std::lock_guard<std::mutex> lock(weather_data_.mutex);
    if (weather_data_.has_data)
    {
        // Draw weather icon
        DrawWeatherIcon(canvas, weather_data_.icon, x_, y_);

        // Draw temperature
        std::string temp_str = std::to_string(static_cast<int>(round(weather_data_.temperature))) + "°F";
        Color temp_color(temp_color_.r, temp_color_.g, temp_color_.b);
        rgb_matrix::DrawText(canvas, font_, x_ + 31, y_ + 22, temp_color, temp_str.c_str());
    }
    else
    {
        Color temp_color(temp_color_.r, temp_color_.g, temp_color_.b);
        rgb_matrix::DrawText(canvas, font_, x_ + 2, y_ + 15, temp_color, "No Data");
    }
}

void WeatherOverlay::Cleanup()
{
    // Stop background thread
    if (background_thread_running_.load())
    {
        background_thread_running_.store(false);
        if (background_thread_.joinable())
        {
            background_thread_.join();
            fprintf(stderr, "Stopped background weather updates\n");
        }
    }
    initialized_.store(false);
}

json WeatherOverlay::FetchWeatherData()
{
    CURL *curl = curl_easy_init();
    if (!curl)
        return json::object();

    std::string url = "https://api.openweathermap.org/data/3.0/onecall?lat=" + std::to_string(lat_) + "&lon=" + std::to_string(lon_) + "&units=imperial&exclude=minutely%2Chourly%2Cdaily&appid=" + api_key_;
    std::string response;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        return json::object();

    try
    {
        return json::parse(response);
    }
    catch (...)
    {
        return json::object();
    }
}

WeatherIcon WeatherOverlay::FetchWeatherIcon(const std::string &icon_code)
{
    WeatherIcon icon;
    icon.icon_code = icon_code;

#ifdef MAGICK_AVAILABLE
    CURL *curl = curl_easy_init();
    if (!curl)
        return icon;

    std::string url = "https://openweathermap.org/img/wn/" + icon_code + "@2x.png";
    std::string response;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        return icon;

    try
    {
        // Create temp file
        std::string temp_file = "/tmp/weather_icon.png";
        std::ofstream out(temp_file, std::ios::binary);
        out.write(response.c_str(), response.size());
        out.close();

        // Load with ImageMagick
        Magick::Image image;
        image.read(temp_file);
        image.resize(Magick::Geometry(32, 32));

        icon.width = image.columns();
        icon.height = image.rows();
        icon.pixels.resize(icon.width * icon.height * 4);

        // Convert to RGBA
        for (size_t i = 0; i < image.rows(); i++)
        {
            for (size_t j = 0; j < image.columns(); j++)
            {
                Magick::ColorRGB color = image.pixelColor(j, i);
                size_t idx = (i * icon.width + j) * 4;
                icon.pixels[idx] = static_cast<uint8_t>(color.red() * 255);
                icon.pixels[idx + 1] = static_cast<uint8_t>(color.green() * 255);
                icon.pixels[idx + 2] = static_cast<uint8_t>(color.blue() * 255);
                icon.pixels[idx + 3] = 255;
            }
        }

        std::remove(temp_file.c_str());
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "Error processing weather icon: %s\n", e.what());
    }
#endif

    return icon;
}

void WeatherOverlay::DrawWeatherIcon(Canvas *canvas, const WeatherIcon &icon, int x, int y)
{
    if (icon.pixels.empty())
        return;

    for (int i = 0; i < icon.height; i++)
    {
        for (int j = 0; j < icon.width; j++)
        {
            size_t idx = (i * icon.width + j) * 4;
            // Only set pixel if not black
            if (icon.pixels[idx] > 0 || icon.pixels[idx + 1] > 0 || icon.pixels[idx + 2] > 0)
            {
                canvas->SetPixel(x + j, y + i,
                                 icon.pixels[idx],
                                 icon.pixels[idx + 1],
                                 icon.pixels[idx + 2]);
            }
        }
    }
}

void WeatherOverlay::BackgroundWeatherUpdate()
{
    // Fetch initial weather data immediately
    if (!api_key_.empty())
    {
        json data = FetchWeatherData();
        if (!data.empty())
        {
            try
            {
                std::lock_guard<std::mutex> lock(weather_data_.mutex);
                weather_data_.temperature = data["current"]["temp"].get<double>();
                weather_data_.icon_code = data["current"]["weather"][0]["icon"].get<std::string>();
                weather_data_.icon = FetchWeatherIcon(weather_data_.icon_code);
                weather_data_.has_data = true;
                fprintf(stderr, "Initial weather fetch: %.1f°F\n", weather_data_.temperature);
            }
            catch (const std::exception &e)
            {
                fprintf(stderr, "Error processing initial weather data: %s\n", e.what());
            }
        }
    }

    // Main update loop
    while (background_thread_running_.load())
    {
        // Sleep for the update interval (in smaller chunks to respond to stop signal quickly)
        for (int i = 0; i < static_cast<int>(update_interval_) && background_thread_running_.load(); i++)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        // Fetch weather data if still running
        if (background_thread_running_.load() && !api_key_.empty())
        {
            json data = FetchWeatherData();
            if (!data.empty())
            {
                try
                {
                    double new_temp = data["current"]["temp"].get<double>();
                    std::string new_icon_code = data["current"]["weather"][0]["icon"].get<std::string>();
                    WeatherIcon new_icon;

                    // Only fetch new icon if code changed
                    if (new_icon_code != weather_data_.icon_code)
                    {
                        new_icon = FetchWeatherIcon(new_icon_code);
                    }
                    else
                    {
                        new_icon = weather_data_.icon;
                    }

                    // Update atomically
                    {
                        std::lock_guard<std::mutex> lock(weather_data_.mutex);
                        weather_data_.temperature = new_temp;
                        weather_data_.icon_code = new_icon_code;
                        weather_data_.icon = new_icon;
                        weather_data_.has_data = true;
                    }

                    fprintf(stderr, "Background weather update: %.1f°F\n", weather_data_.temperature);
                }
                catch (const std::exception &e)
                {
                    fprintf(stderr, "Error processing background weather data: %s\n", e.what());
                }
            }
        }
    }
}

size_t WeatherOverlay::WriteCallback(void *contents, size_t size, size_t nmemb, std::string *userp)
{
    userp->append((char *)contents, size * nmemb);
    return size * nmemb;
}

// TextOverlay Implementation
TextOverlay::TextOverlay(const std::string &name, const std::string &text, const VisualColor &color)
    : Overlay(name), text_(text), text_color_(color), font_file_("../fonts/8x13B.bdf"), font_loaded_(false)
{
}

void TextOverlay::Initialize()
{
    font_loaded_ = font_.LoadFont(font_file_.c_str());
    if (!font_loaded_)
    {
        fprintf(stderr, "Warning: Couldn't load font '%s' for text overlay '%s'\n",
                font_file_.c_str(), GetName().c_str());
    }
}

void TextOverlay::Update(float delta_time)
{
    // Text overlays are typically static, but this can be overridden
}

void TextOverlay::Draw(Canvas *canvas)
{
    if (font_loaded_ && !text_.empty())
    {
        Color color(text_color_.r, text_color_.g, text_color_.b);
        rgb_matrix::DrawText(canvas, font_, x_, y_, color, text_.c_str());
    }
}

// MarqueeTextOverlay Implementation
MarqueeTextOverlay::MarqueeTextOverlay(const std::string &name, const std::string &text,
                                       const VisualColor &color, int max_display_width, int character_width)
    : TextOverlay(name, text, color), max_display_width_(max_display_width), character_width_(character_width),
      scroll_speed_(20.0f), pause_duration_(2.0f), scroll_offset_(0.0f),
      pause_timer_(0.0f), text_width_(0), is_scrolling_(false),
      needs_scrolling_(false), scroll_direction_(true), buffer_width_(0), buffer_height_(0)
{
}

void MarqueeTextOverlay::Initialize()
{
    TextOverlay::Initialize();

    if (font_loaded_)
    {
        ResetScrolling();
    }
}

void MarqueeTextOverlay::Update(float delta_time)
{
    if (!font_loaded_ || text_.empty() || !needs_scrolling_)
        return;

    if (!is_scrolling_)
    {
        // Pause at beginning
        pause_timer_ += delta_time;
        if (pause_timer_ >= pause_duration_)
        {
            is_scrolling_ = true;
            pause_timer_ = 0.0f;
        }
    }
    else
    {
        // Scroll the text based on direction
        if (scroll_direction_) // Right-to-left
        {
            scroll_offset_ += scroll_speed_ * delta_time;

            // If we've scrolled past the end, reverse direction and pause
            if (scroll_offset_ >= (text_width_ - max_display_width_))
            {
                scroll_offset_ = text_width_ - max_display_width_;
                scroll_direction_ = false; // Switch to left-to-right
                is_scrolling_ = false;     // Pause before reversing
                pause_timer_ = 0.0f;
            }
        }
        else // Left-to-right
        {
            scroll_offset_ -= scroll_speed_ * delta_time;

            // If we've scrolled back to the beginning, reverse direction and pause
            if (scroll_offset_ <= 0.0f)
            {
                scroll_offset_ = 0.0f;
                scroll_direction_ = true; // Switch to right-to-left
                is_scrolling_ = false;    // Pause before reversing
                pause_timer_ = 0.0f;
            }
        }
    }
}

void MarqueeTextOverlay::Draw(Canvas *canvas)
{
    if (!font_loaded_ || text_.empty())
        return;

    Color color(text_color_.r, text_color_.g, text_color_.b);

    if (!needs_scrolling_)
    {
        // Text fits, draw normally
        rgb_matrix::DrawText(canvas, font_, x_, y_, color, text_.c_str());
    }
    else
    {
        // Text needs scrolling, draw with clipping (bypasses buffer since DrawScrollingText renders directly)
        DrawScrollingText(canvas, color);
    }
}

void MarqueeTextOverlay::SetText(const std::string &text)
{
    // Only reset scrolling if text actually changed
    if (text_ != text)
    {
        text_ = text;
        if (font_loaded_)
        {
            ResetScrolling();
        }
    }
    else
    {
        // Text is the same, just update the text_ field to ensure it's set
        text_ = text;
    }
}

void MarqueeTextOverlay::RenderTextToBuffer()
{
    if (!font_loaded_ || text_.empty())
        return;

    // Calculate buffer dimensions
    text_width_ = CalculateTextWidth(text_);
    buffer_width_ = text_width_ + max_display_width_; // Extra space for smooth scrolling
    buffer_height_ = font_.height();

    // Create buffer (RGB, 3 bytes per pixel)
    text_buffer_.clear();
    text_buffer_.resize(buffer_width_ * buffer_height_ * 3, 0);

    // Create a temporary canvas for rendering
    // We'll simulate this by creating a simple RGB buffer and manually drawing pixels
    // This is a simplified version - in a real implementation you might want to use
    // a proper off-screen canvas

    // For now, we'll use a simpler approach: calculate the character positions
    // and render them with clipping in the Draw method
}

void MarqueeTextOverlay::ResetScrolling()
{
    scroll_offset_ = 0.0f;
    pause_timer_ = 0.0f;
    is_scrolling_ = false;
    scroll_direction_ = true; // Always start right-to-left

    if (font_loaded_)
    {
        text_width_ = CalculateTextWidth(text_);
        needs_scrolling_ = text_width_ > max_display_width_;
    }
}

int MarqueeTextOverlay::CalculateTextWidth(const std::string &text)
{
    if (text.empty())
        return 0;

    // Simple calculation using fixed character width
    return text.length() * character_width_;
}

void MarqueeTextOverlay::DrawScrollingText(Canvas *canvas, const Color &color)
{
    // Draw text with custom pixel-level clipping to marquee bounds
    int current_x = x_ - static_cast<int>(scroll_offset_);
    int marquee_left = x_;
    int marquee_right = x_ + max_display_width_;

    for (char c : text_)
    {
        // Skip non-printable characters
        if (c < 32 || c > 126)
            continue;

        // Draw character with custom clipping to marquee bounds
        DrawClippedGlyph(canvas, current_x, y_, color, c, marquee_left, marquee_right);

        current_x += character_width_;

        // Early exit optimization - if character start is way past marquee, stop
        if (current_x > marquee_right + character_width_)
            break;
    }
}

int MarqueeTextOverlay::DrawClippedGlyph(Canvas *canvas, int x_pos, int y_pos, const Color &color,
                                         char glyph, int clip_left, int clip_right)
{
    // Manual glyph drawing with pixel-perfect clipping
    // This creates a Canvas subclass that clips SetPixel calls to marquee bounds

    int char_width = font_.CharacterWidth(glyph);
    if (char_width <= 0)
        return 0;

    // Check if character is completely outside marquee bounds
    if (x_pos + char_width <= clip_left || x_pos >= clip_right)
    {
        return char_width;
    }

    // Create a clipping wrapper that inherits from Canvas
    class ClippedCanvas : public Canvas
    {
    private:
        Canvas *target_;
        int clip_left_, clip_right_;

    public:
        ClippedCanvas(Canvas *target, int clip_left, int clip_right)
            : target_(target), clip_left_(clip_left), clip_right_(clip_right) {}

        void SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) override
        {
            if (x >= clip_left_ && x < clip_right_)
            {
                target_->SetPixel(x, y, r, g, b);
            }
        }

        int width() const override { return target_->width(); }
        int height() const override { return target_->height(); }

        // Required pure virtual methods (unused but must be implemented)
        void Clear() override {}
        void Fill(uint8_t r, uint8_t g, uint8_t b) override {}
    };

    // Create our clipping canvas and draw the glyph through it
    ClippedCanvas clipped_canvas(canvas, clip_left, clip_right);

    // Use the font's DrawGlyph method with our clipping canvas for pixel-perfect clipping
    return font_.DrawGlyph(&clipped_canvas, x_pos, y_pos, color, glyph);
}

// SpotifyOverlay Implementation
SpotifyOverlay::SpotifyOverlay(const std::string &name, const std::string &client_id,
                               const std::string &client_secret, const std::string &refresh_token)
    : Overlay(name), client_id_(client_id), client_secret_(client_secret), refresh_token_(refresh_token),
      polling_interval_(5.0f), time_since_poll_(0.0f), time_since_token_refresh_(0.0f),
      text_color_(255, 255, 255), api_call_in_progress_(false), force_text_refresh_(false)
{
}

void SpotifyOverlay::Initialize()
{
    // Load font
    if (!font_.LoadFont("../fonts/4x6.bdf"))
    {
        fprintf(stderr, "Warning: Couldn't load font for Spotify overlay\n");
    }

    // Create marquee overlays for scrolling text (after font is loaded)
    track_marquee_ = std::make_unique<MarqueeTextOverlay>(
        GetName() + "_track_marquee", "", VisualColor(255, 255, 255), 32); // 32px max width
    artist_marquee_ = std::make_unique<MarqueeTextOverlay>(
        GetName() + "_artist_marquee", "", VisualColor(200, 200, 200), 32); // Slightly dimmer for artist

    // Set the marquee overlays to use the same font as SpotifyOverlay
    track_marquee_->SetFontFile("../fonts/4x6.bdf");
    artist_marquee_->SetFontFile("../fonts/4x6.bdf");

    // Configure scrolling parameters
    const float scroll_speed = 7.5f;
    const float pause_duration = 4.0f;
    track_marquee_->SetScrollSpeed(scroll_speed);
    track_marquee_->SetPauseDuration(pause_duration);
    artist_marquee_->SetScrollSpeed(scroll_speed);
    artist_marquee_->SetPauseDuration(pause_duration);

    // Initialize marquee overlays
    track_marquee_->Initialize();
    artist_marquee_->Initialize();

    // Get initial access token
    if (!refresh_token_.empty())
    {
        access_token_ = RefreshAccessToken();
        if (!access_token_.empty())
        {
            // Initial track fetch
            json data = FetchCurrentlyPlaying();
            if (!data.empty() && !data["item"].is_null())
            {
                try
                {
                    // Validate JSON structure before accessing
                    if (!data.contains("item") || data["item"].is_null() ||
                        !data.contains("is_playing") ||
                        !data["item"].contains("name") ||
                        !data["item"].contains("artists"))
                    {
                        fprintf(stderr, "Invalid Spotify JSON structure in initial fetch\n");
                        return;
                    }

                    std::lock_guard<std::mutex> lock(current_track_.mutex);
                    current_track_.track_name = data["item"]["name"].get<std::string>();
                    // Safely get artist name with bounds checking
                    if (!data["item"]["artists"].empty() && data["item"]["artists"].is_array())
                    {
                        current_track_.artist_name = data["item"]["artists"][0]["name"].get<std::string>();
                    }
                    else
                    {
                        current_track_.artist_name = "Unknown Artist";
                    }
                    current_track_.is_playing = data["is_playing"].get<bool>();
                    current_track_.progress_ms = data.contains("progress_ms") && !data["progress_ms"].is_null() ? data["progress_ms"].get<int>() : 0;
                    current_track_.duration_ms = data["item"].contains("duration_ms") && !data["item"]["duration_ms"].is_null() ? data["item"]["duration_ms"].get<int>() : 0;

                    fprintf(stderr, "Initial Spotify data - Progress: %d ms, Duration: %d ms\n", current_track_.progress_ms, current_track_.duration_ms);

                    // Get album art URL (use smallest image)
                    if (data["item"].contains("album") && data["item"]["album"].contains("images") &&
                        !data["item"]["album"]["images"].empty())
                    {
                        auto images = data["item"]["album"]["images"];
                        current_track_.album_art_url = images.back()["url"].get<std::string>();
                        current_track_.album_art = FetchAlbumArt(current_track_.album_art_url);
                    }

                    current_track_.has_data = true;
                }
                catch (const std::exception &e)
                {
                    fprintf(stderr, "Error processing initial Spotify data: %s\n", e.what());
                }
            }
        }
    }
}

void SpotifyOverlay::Update(float delta_time)
{
    time_since_poll_ += delta_time;
    time_since_token_refresh_ += delta_time;

    // Refresh access token every 55 minutes (3300 seconds) to ensure buffer (non-blocking)
    if (time_since_token_refresh_ >= 3300.0f && !refresh_token_.empty() && !api_call_in_progress_.load())
    {
        api_call_in_progress_.store(true);
        auto token_future = std::async(std::launch::async, [this]()
                                       {
            std::string new_token = RefreshAccessToken();
            if (!new_token.empty())
            {
                access_token_ = new_token;
                time_since_token_refresh_ = 0.0f;
                fprintf(stderr, "Refreshed Spotify access token\n");
            }
            api_call_in_progress_.store(false); });
        // Don't need to wait for token refresh, just prevent warning
        (void)token_future;
    }

    // Poll for currently playing track (non-blocking)
    if (time_since_poll_ >= polling_interval_ && !access_token_.empty() && !api_call_in_progress_.load())
    {
        // Start async API call
        api_call_in_progress_.store(true);
        api_future_ = std::async(std::launch::async, [this]()
                                 {
            json data = FetchCurrentlyPlaying();
            if (!data.empty())
            {
                try
                {
                    std::string new_track_name;
                    std::string new_artist_name;
                    std::string new_album_art_url;
                    bool new_is_playing = false;
                    int new_progress_ms = 0;
                    int new_duration_ms = 0;
                    SpotifyAlbumArt new_album_art;

                    if (!data["item"].is_null())
                    {
                        new_track_name = data["item"]["name"].get<std::string>();
                        // Safely get artist name with bounds checking
                        if (!data["item"]["artists"].empty() && data["item"]["artists"].is_array()) 
                        {
                            new_artist_name = data["item"]["artists"][0]["name"].get<std::string>();
                        }
                        else
                        {
                            new_artist_name = "Unknown Artist";
                        }
                        new_is_playing = data["is_playing"].get<bool>();
                        new_progress_ms = data.contains("progress_ms") && !data["progress_ms"].is_null() ? data["progress_ms"].get<int>() : 0;
                        new_duration_ms = data["item"].contains("duration_ms") && !data["item"]["duration_ms"].is_null() ? data["item"]["duration_ms"].get<int>() : 0;

                        // Get album art URL (use smallest image for faster download)
                        if (!data["item"]["album"]["images"].empty())
                        {
                            auto images = data["item"]["album"]["images"];
                            new_album_art_url = images.back()["url"].get<std::string>();

                            // Only fetch new album art if URL changed
                            if (new_album_art_url != current_track_.album_art_url)
                            {
                                new_album_art = FetchAlbumArt(new_album_art_url);
                            }
                            else
                            {
                                new_album_art = current_track_.album_art;
                            }
                        }
                    }

                    // Update track data atomically
                    {
                        std::lock_guard<std::mutex> lock(current_track_.mutex);
                        current_track_.track_name = new_track_name;
                        current_track_.artist_name = new_artist_name;
                        current_track_.album_art_url = new_album_art_url;
                        current_track_.album_art = new_album_art;
                        current_track_.is_playing = new_is_playing;
                        current_track_.progress_ms = new_progress_ms;
                        current_track_.duration_ms = new_duration_ms;
                        current_track_.has_data = !new_track_name.empty();
                    }

                    fprintf(stderr, "Updated Spotify: %s by %s (%s) - Progress: %d/%d ms\n",
                            new_track_name.c_str(), new_artist_name.c_str(),
                            new_is_playing ? "playing" : "paused", new_progress_ms, new_duration_ms);
                }
                catch (const std::exception &e)
                {
                    fprintf(stderr, "Error processing Spotify data: %s\n", e.what());
                }
            }
            api_call_in_progress_.store(false); });
        time_since_poll_ = 0.0f;
    }

    // Update marquee text (only if marquee is initialized)
    if (track_marquee_ && artist_marquee_)
    {
        std::lock_guard<std::mutex> lock(current_track_.mutex);
        if (current_track_.has_data)
        {
            // Always set text to ensure it's properly initialized after screen switches
            // SetText() will only reset scrolling if text actually changed
            track_marquee_->SetText(current_track_.track_name);
            artist_marquee_->SetText(current_track_.artist_name);

            // Update tracking variables
            last_track_name_ = current_track_.track_name;
            last_artist_name_ = current_track_.artist_name;

            // Reset the force refresh flag after handling it
            force_text_refresh_ = false;
        }
    }

    // Update marquee animations (only if initialized)
    if (track_marquee_ && artist_marquee_)
    {
        track_marquee_->SetPosition(x_ + 30, y_ + 7);
        artist_marquee_->SetPosition(x_ + 30, y_ + 14);
        track_marquee_->Update(delta_time);
        artist_marquee_->Update(delta_time);
    }
}

void SpotifyOverlay::Draw(Canvas *canvas)
{
    std::lock_guard<std::mutex> lock(current_track_.mutex);
    if (current_track_.has_data)
    {
        // Set flag to force text refresh on next update (handles screen switching)
        if (!force_text_refresh_)
        {
            force_text_refresh_ = true;
        }
        // Draw album art (24x24 at top-left)
        DrawAlbumArt(canvas, current_track_.album_art, x_, y_);

        // Draw scrolling text using marquee overlays (fallback to direct text if not initialized)
        if (track_marquee_ && artist_marquee_)
        {
            track_marquee_->Draw(canvas);
            artist_marquee_->Draw(canvas);
        }
        else
        {
            // Fallback to direct text rendering if marquee not initialized
            Color text_color(text_color_.r, text_color_.g, text_color_.b);
            rgb_matrix::DrawText(canvas, font_, x_ + 30, y_ + 7, text_color, current_track_.track_name.c_str());
            rgb_matrix::DrawText(canvas, font_, x_ + 30, y_ + 14, text_color, current_track_.artist_name.c_str());
        }

        // Draw progress bar (3 pixels tall, 32 pixels wide, positioned below the text)
        if (current_track_.duration_ms > 0)
        {
            float progress = static_cast<float>(current_track_.progress_ms) / static_cast<float>(current_track_.duration_ms);
            DrawProgressBar(canvas, x_ + 30, y_ + 20, 30, 2, progress);
        }
    }
    else
    {
        Color text_color(text_color_.r, text_color_.g, text_color_.b);
        rgb_matrix::DrawText(canvas, font_, x_ + 2, y_ + 15, text_color, "No Music");
    }
}

void SpotifyOverlay::Cleanup()
{
    // Nothing specific to cleanup
}

std::string SpotifyOverlay::RefreshAccessToken()
{
    CURL *curl = curl_easy_init();
    if (!curl)
        return "";

    std::string auth_string = client_id_ + ":" + client_secret_;
    std::string auth_header = "Authorization: Basic " + Base64Encode(auth_string);
    std::string post_data = "grant_type=refresh_token&refresh_token=" + refresh_token_;
    std::string response;

    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    headers = curl_slist_append(headers, auth_header.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, "https://accounts.spotify.com/api/token");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        return "";

    try
    {
        json token_response = json::parse(response);
        return token_response["access_token"].get<std::string>();
    }
    catch (...)
    {
        return "";
    }
}

json SpotifyOverlay::FetchCurrentlyPlaying()
{
    CURL *curl = curl_easy_init();
    if (!curl)
        return json::object();

    std::string auth_header = "Authorization: Bearer " + access_token_;
    std::string response;

    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, auth_header.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.spotify.com/v1/me/player/currently-playing");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || response.empty())
        return json::object();

    try
    {
        return json::parse(response);
    }
    catch (...)
    {
        return json::object();
    }
}

SpotifyAlbumArt SpotifyOverlay::FetchAlbumArt(const std::string &image_url)
{
    SpotifyAlbumArt art;
    art.url = image_url;

#ifdef MAGICK_AVAILABLE
    CURL *curl = curl_easy_init();
    if (!curl)
        return art;

    std::string response;

    curl_easy_setopt(curl, CURLOPT_URL, image_url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        return art;

    try
    {
        // Create temp file
        std::string temp_file = "/tmp/spotify_album_art.jpg";
        std::ofstream out(temp_file, std::ios::binary);
        out.write(response.c_str(), response.size());
        out.close();

        // Load with ImageMagick
        Magick::Image image;
        image.read(temp_file);
        image.resize(Magick::Geometry(28, 28));
        image.sigmoidalContrast(true, 7.0); // heavily increase contrast

        art.width = image.columns();
        art.height = image.rows();
        art.pixels.resize(art.width * art.height * 4);

        // Convert to RGBA
        for (size_t i = 0; i < image.rows(); i++)
        {
            for (size_t j = 0; j < image.columns(); j++)
            {
                Magick::ColorRGB color = image.pixelColor(j, i);
                size_t idx = (i * art.width + j) * 4;
                art.pixels[idx] = static_cast<uint8_t>(color.red() * 255);
                art.pixels[idx + 1] = static_cast<uint8_t>(color.green() * 255);
                art.pixels[idx + 2] = static_cast<uint8_t>(color.blue() * 255);
                art.pixels[idx + 3] = 255;
            }
        }

        std::remove(temp_file.c_str());
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "Error processing album art: %s\n", e.what());
    }
#endif

    return art;
}

void SpotifyOverlay::DrawAlbumArt(Canvas *canvas, const SpotifyAlbumArt &art, int x, int y)
{
    if (art.pixels.empty())
        return;

    for (int i = 0; i < art.height; i++)
    {
        for (int j = 0; j < art.width; j++)
        {
            size_t idx = (i * art.width + j) * 4;
            canvas->SetPixel(x + j, y + i,
                             art.pixels[idx],
                             art.pixels[idx + 1],
                             art.pixels[idx + 2]);
        }
    }
}

void SpotifyOverlay::DrawProgressBar(Canvas *canvas, int x, int y, int width, int height, float progress)
{
    // Validate inputs
    if (!canvas || width <= 0 || height <= 0)
        return;

    // Clamp progress between 0 and 1
    progress = std::max(0.0f, std::min(1.0f, progress));

    // Get canvas dimensions for strict bounds checking
    const int canvas_width = canvas->width();
    const int canvas_height = canvas->height();

    // Calculate filled width
    const int filled_width = static_cast<int>(progress * width);

    // Draw the progress bar with very strict bounds checking
    for (int row = 0; row < height; row++)
    {
        const int pixel_y = y + row;
        // Skip if row is outside canvas
        if (pixel_y < 0 || pixel_y >= canvas_height)
            continue;

        for (int col = 0; col < width; col++)
        {
            const int pixel_x = x + col;
            // Skip if column is outside canvas
            if (pixel_x < 0 || pixel_x >= canvas_width)
                continue;

            if (col < filled_width)
            {
                // Filled portion - almost white
                canvas->SetPixel(pixel_x, pixel_y, 240, 240, 240);
            }
            else
            {
                // Empty portion - dark gray
                canvas->SetPixel(pixel_x, pixel_y, 32, 32, 32);
            }
        }
    }
}

std::string SpotifyOverlay::Base64Encode(const std::string &input)
{
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    int val = 0, valb = -6;
    for (unsigned char c : input)
    {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0)
        {
            encoded.push_back(chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6)
        encoded.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (encoded.size() % 4)
        encoded.push_back('=');
    return encoded;
}

void SpotifyOverlay::SetCredentials(const std::string &client_id, const std::string &client_secret, const std::string &refresh_token)
{
    client_id_ = client_id;
    client_secret_ = client_secret;
    refresh_token_ = refresh_token;
}

size_t SpotifyOverlay::WriteCallback(void *contents, size_t size, size_t nmemb, std::string *userp)
{
    size_t total_size = size * nmemb;
    userp->append(static_cast<char *>(contents), total_size);
    return total_size;
}