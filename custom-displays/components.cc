#include "components.h"
#include <fstream>
#include <sstream>
#include <cmath>
#include <iostream>
#include <mutex>
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
      update_interval_(300.0f), time_since_update_(0.0f),
      temp_color_(178, 226, 206)
{
}

void WeatherOverlay::Initialize()
{
    // Load font
    if (!font_.LoadFont("../fonts/8x13B.bdf"))
    {
        fprintf(stderr, "Warning: Couldn't load font for weather overlay\n");
    }

    // Initial weather fetch
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
            }
            catch (const std::exception &e)
            {
                fprintf(stderr, "Error processing initial weather data: %s\n", e.what());
            }
        }
    }
}

void WeatherOverlay::Update(float delta_time)
{
    time_since_update_ += delta_time;

    // Update weather data at interval
    if (time_since_update_ >= update_interval_ && !api_key_.empty())
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

                fprintf(stderr, "Updated temperature to %.1f°F\n", weather_data_.temperature);
            }
            catch (const std::exception &e)
            {
                fprintf(stderr, "Error processing weather data: %s\n", e.what());
            }
        }
        time_since_update_ = 0.0f;
    }
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
    // Nothing specific to cleanup
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
      needs_scrolling_(false), buffer_width_(0), buffer_height_(0)
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
        // Scroll the text
        scroll_offset_ += scroll_speed_ * delta_time;

        // If we've scrolled past the end, reset
        if (scroll_offset_ >= text_width_)
        {
            ResetScrolling();
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
        // Text needs scrolling, draw from buffer with offset
        if (!text_buffer_.empty())
        {
            DrawScrollingText(canvas, color);
        }
    }
}

void MarqueeTextOverlay::SetText(const std::string &text)
{
    text_ = text;
    if (font_loaded_)
    {
        ResetScrolling();
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

    if (font_loaded_)
    {
        text_width_ = CalculateTextWidth(text_);
        needs_scrolling_ = text_width_ > max_display_width_;

        if (needs_scrolling_)
        {
            RenderTextToBuffer();
        }
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
    // Draw text with pixel-perfect clipping using fixed character width
    int current_x = x_ - static_cast<int>(scroll_offset_);

    for (char c : text_)
    {
        if (c < 32 || c > 126) // Skip non-printable characters
            continue;

        // Check if this character is visible in the display area
        if (current_x + character_width_ > x_ && current_x < x_ + max_display_width_)
        {
            // Draw the character if it's at least partially visible
            if (current_x >= x_ - character_width_ && current_x < x_ + max_display_width_)
            {
                std::string single_char(1, c);
                rgb_matrix::DrawText(canvas, font_, current_x, y_, color, single_char.c_str());
            }
        }

        current_x += character_width_;

        // Early exit if we're past the visible area
        if (current_x >= x_ + max_display_width_)
            break;
    }
}

// SpotifyOverlay Implementation
SpotifyOverlay::SpotifyOverlay(const std::string &name, const std::string &client_id,
                               const std::string &client_secret, const std::string &refresh_token)
    : Overlay(name), client_id_(client_id), client_secret_(client_secret), refresh_token_(refresh_token),
      polling_interval_(5.0f), time_since_poll_(0.0f), time_since_token_refresh_(0.0f),
      text_color_(255, 255, 255)
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
    track_marquee_->SetScrollSpeed(12.0f);
    track_marquee_->SetPauseDuration(2.0f);
    artist_marquee_->SetScrollSpeed(12.0f);
    artist_marquee_->SetPauseDuration(2.0f);

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
                    std::lock_guard<std::mutex> lock(current_track_.mutex);
                    current_track_.track_name = data["item"]["name"].get<std::string>();
                    current_track_.artist_name = data["item"]["artists"][0]["name"].get<std::string>();
                    current_track_.is_playing = data["is_playing"].get<bool>();

                    // Get album art URL (use smallest image)
                    if (!data["item"]["album"]["images"].empty())
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

    // Refresh access token every 55 minutes (3300 seconds) to ensure buffer
    if (time_since_token_refresh_ >= 3300.0f && !refresh_token_.empty())
    {
        std::string new_token = RefreshAccessToken();
        if (!new_token.empty())
        {
            access_token_ = new_token;
            time_since_token_refresh_ = 0.0f;
            fprintf(stderr, "Refreshed Spotify access token\n");
        }
    }

    // Poll for currently playing track
    if (time_since_poll_ >= polling_interval_ && !access_token_.empty())
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
                SpotifyAlbumArt new_album_art;

                if (!data["item"].is_null())
                {
                    new_track_name = data["item"]["name"].get<std::string>();
                    new_artist_name = data["item"]["artists"][0]["name"].get<std::string>();
                    new_is_playing = data["is_playing"].get<bool>();

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
                    current_track_.has_data = !new_track_name.empty();
                }

                fprintf(stderr, "Updated Spotify: %s by %s (%s)\n",
                        new_track_name.c_str(), new_artist_name.c_str(),
                        new_is_playing ? "playing" : "paused");
            }
            catch (const std::exception &e)
            {
                fprintf(stderr, "Error processing Spotify data: %s\n", e.what());
            }
        }
        time_since_poll_ = 0.0f;
    }

    // Update marquee text if track info changed (only if marquee is initialized)
    if (track_marquee_ && artist_marquee_)
    {
        std::lock_guard<std::mutex> lock(current_track_.mutex);
        if (current_track_.has_data)
        {
            if (current_track_.track_name != last_track_name_)
            {
                track_marquee_->SetText(current_track_.track_name);
                last_track_name_ = current_track_.track_name;
            }
            if (current_track_.artist_name != last_artist_name_)
            {
                artist_marquee_->SetText(current_track_.artist_name);
                last_artist_name_ = current_track_.artist_name;
            }
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