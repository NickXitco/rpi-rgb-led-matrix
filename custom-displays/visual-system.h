#pragma once

#include "led-matrix.h"
#include <chrono>
#include <memory>
#include <vector>
#include <string>

using namespace rgb_matrix;
using namespace std::chrono;

// Forward declarations
class VisualComponent;
class Background;
class Overlay;
class Screen;

// Color struct for the visual system
struct VisualColor
{
    uint8_t r, g, b, a;
    VisualColor(uint8_t r = 0, uint8_t g = 0, uint8_t b = 0, uint8_t a = 255)
        : r(r), g(g), b(b), a(a) {}
};

// Base class for all visual components
class VisualComponent
{
public:
    VisualComponent(const std::string &name) : name_(name), enabled_(true) {}
    virtual ~VisualComponent() = default;

    // Core lifecycle methods
    virtual void Initialize() {}
    virtual void Update(float delta_time) = 0;
    virtual void Draw(Canvas *canvas) = 0;
    virtual void Cleanup() {}

    // Component management
    const std::string &GetName() const { return name_; }
    bool IsEnabled() const { return enabled_; }
    void SetEnabled(bool enabled) { enabled_ = enabled; }

protected:
    std::string name_;
    bool enabled_;
};

// Background components fill the entire canvas
class Background : public VisualComponent
{
public:
    Background(const std::string &name) : VisualComponent(name) {}
    virtual ~Background() = default;

    // Backgrounds typically have configurable parameters
    virtual void SetParameter(const std::string &param, float value) {}
    virtual void SetColor(const VisualColor &color) {}
};

// Overlay components draw on top of backgrounds
class Overlay : public VisualComponent
{
public:
    Overlay(const std::string &name) : VisualComponent(name) {}
    virtual ~Overlay() = default;

    // Overlays can have position and size
    virtual void SetPosition(int x, int y)
    {
        x_ = x;
        y_ = y;
    }
    virtual void SetSize(int width, int height)
    {
        width_ = width;
        height_ = height;
    }

protected:
    int x_ = 0, y_ = 0;
    int width_ = 0, height_ = 0;
};

// Screen manages a collection of visual components
class Screen
{
public:
    Screen(const std::string &name) : name_(name) {}
    virtual ~Screen() = default;

    // Component management
    void AddBackground(std::shared_ptr<Background> background);
    void AddOverlay(std::shared_ptr<Overlay> overlay);
    void RemoveComponent(const std::string &name);

    // Lifecycle
    virtual void Initialize();
    virtual void Update(float delta_time);
    virtual void Draw(Canvas *canvas);
    virtual void Cleanup();

    const std::string &GetName() const { return name_; }

private:
    std::string name_;
    std::vector<std::shared_ptr<Background>> backgrounds_;
    std::vector<std::shared_ptr<Overlay>> overlays_;
};

// Display manager coordinates screens and handles transitions
class DisplayManager
{
public:
    DisplayManager(RGBMatrix *matrix);
    ~DisplayManager();

    // Screen management
    void AddScreen(std::shared_ptr<Screen> screen);
    void SetActiveScreen(const std::string &name);
    std::shared_ptr<Screen> GetActiveScreen() const { return active_screen_; }

    // Main loop
    void Run();
    void Stop() { running_ = false; }

private:
    RGBMatrix *matrix_;
    FrameCanvas *canvas_;
    std::vector<std::shared_ptr<Screen>> screens_;
    std::shared_ptr<Screen> active_screen_;
    bool running_;

    high_resolution_clock::time_point last_frame_;
};