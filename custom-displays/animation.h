#pragma once

#include "led-matrix.h"
#include <chrono>
#include <random>

#define STB_PERLIN_IMPLEMENTATION
#include "external/stb_perlin.h"

using namespace rgb_matrix;
using namespace std::chrono;

class Animation {
public:
    Animation(Canvas *canvas) : canvas_(canvas) {}
    virtual ~Animation() = default;

    // Update the animation state
    virtual void Update(float delta_time) = 0;
    
    // Draw the animation to the canvas
    virtual void Draw() = 0;

protected:
    Canvas *const canvas_;
    inline Canvas *canvas() { return canvas_; }
};

class PerlinNoiseAnimation : public Animation {
public:
    PerlinNoiseAnimation(Canvas *canvas) : Animation(canvas) {
        // Initialize random seed
        std::random_device rd;
        seed = rd();
    }

    void Update(float delta_time) override {
        z += speed * delta_time;
    }

    void Draw() override {
        fprintf(stderr, "Drawing Perlin noise over %dx%d canvas\n", canvas()->width(), canvas()->height());
        for (int x = 0; x < canvas()->width(); ++x) {
            for (int y = 0; y < canvas()->height(); ++y) {
                float nx = x * scale;
                float ny = y * scale;
                float nz = z;
                
                float n = (stb_perlin_noise3(nx, ny, nz, 0, 0, 0) + 1.0f) * 0.5f;
                float eased = (n * n * n);
                uint8_t value = static_cast<uint8_t>(eased * 120);
                
                canvas()->SetPixel(x, y, value * 0.20f, value * 0.10f, value * 1.0f);
            }
        }
        fprintf(stderr, "Finished drawing Perlin noise\n");
    }

private:
    float z = 0.0f;
    const float scale = 0.1f;
    const float speed = 0.5f;
    int seed;
}; 