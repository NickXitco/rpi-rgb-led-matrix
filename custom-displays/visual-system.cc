#include "visual-system.h"
#include <algorithm>
#include <signal.h>

extern volatile bool interrupt_received;

// Screen implementation
void Screen::AddBackground(std::shared_ptr<Background> background)
{
    backgrounds_.push_back(background);
}

void Screen::AddOverlay(std::shared_ptr<Overlay> overlay)
{
    overlays_.push_back(overlay);
}

void Screen::RemoveComponent(const std::string &name)
{
    // Remove from backgrounds
    backgrounds_.erase(
        std::remove_if(backgrounds_.begin(), backgrounds_.end(),
                       [&name](const std::shared_ptr<Background> &bg)
                       {
                           return bg->GetName() == name;
                       }),
        backgrounds_.end());

    // Remove from overlays
    overlays_.erase(
        std::remove_if(overlays_.begin(), overlays_.end(),
                       [&name](const std::shared_ptr<Overlay> &overlay)
                       {
                           return overlay->GetName() == name;
                       }),
        overlays_.end());
}

void Screen::Initialize()
{
    // Initialize all backgrounds
    for (auto &background : backgrounds_)
    {
        if (background->IsEnabled())
        {
            background->Initialize();
        }
    }

    // Initialize all overlays
    for (auto &overlay : overlays_)
    {
        if (overlay->IsEnabled())
        {
            overlay->Initialize();
        }
    }
}

void Screen::Update(float delta_time)
{
    // Update all backgrounds
    for (auto &background : backgrounds_)
    {
        if (background->IsEnabled())
        {
            background->Update(delta_time);
        }
    }

    // Update all overlays
    for (auto &overlay : overlays_)
    {
        if (overlay->IsEnabled())
        {
            overlay->Update(delta_time);
        }
    }
}

void Screen::Draw(Canvas *canvas)
{
    // Draw backgrounds first (in order)
    for (auto &background : backgrounds_)
    {
        if (background->IsEnabled())
        {
            background->Draw(canvas);
        }
    }

    // Draw overlays on top (in order)
    for (auto &overlay : overlays_)
    {
        if (overlay->IsEnabled())
        {
            overlay->Draw(canvas);
        }
    }
}

void Screen::Cleanup()
{
    // Cleanup all backgrounds
    for (auto &background : backgrounds_)
    {
        background->Cleanup();
    }

    // Cleanup all overlays
    for (auto &overlay : overlays_)
    {
        overlay->Cleanup();
    }
}

// DisplayManager implementation
DisplayManager::DisplayManager(RGBMatrix *matrix)
    : matrix_(matrix), running_(false)
{
    canvas_ = matrix_->CreateFrameCanvas();
    last_frame_ = high_resolution_clock::now();
}

DisplayManager::~DisplayManager()
{
    if (active_screen_)
    {
        active_screen_->Cleanup();
    }
}

void DisplayManager::AddScreen(std::shared_ptr<Screen> screen)
{
    screens_.push_back(screen);

    // Set as active if it's the first screen
    if (screens_.size() == 1)
    {
        SetActiveScreen(screen->GetName());
    }
}

void DisplayManager::SetActiveScreen(const std::string &name)
{
    // Cleanup current screen
    if (active_screen_)
    {
        active_screen_->Cleanup();
    }

    // Find and set new active screen
    auto it = std::find_if(screens_.begin(), screens_.end(),
                           [&name](const std::shared_ptr<Screen> &screen)
                           {
                               return screen->GetName() == name;
                           });

    if (it != screens_.end())
    {
        active_screen_ = *it;
        active_screen_->Initialize();
    }
}

void DisplayManager::Run()
{
    running_ = true;

    while (running_ && !interrupt_received)
    {
        auto now = high_resolution_clock::now();
        float delta_time = duration<float>(now - last_frame_).count();
        last_frame_ = now;

        if (active_screen_)
        {
            active_screen_->Update(delta_time);
            active_screen_->Draw(canvas_);
        }

        canvas_ = matrix_->SwapOnVSync(canvas_);
    }
}