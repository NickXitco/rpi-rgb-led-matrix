#include "led-matrix.h"
#include "graphics.h"
#include "external/stb_perlin.h"

#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <random>

using namespace rgb_matrix;

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
  interrupt_received = true;
}

class PerlinNoiseGenerator {
protected:
  PerlinNoiseGenerator(Canvas *canvas) : canvas_(canvas) {
    // Initialize random seed
    std::random_device rd;
    seed = rd();
  }

  inline Canvas *canvas() { return canvas_; }

public:
  virtual ~PerlinNoiseGenerator() {}
  
  void Run() {
    float z = 0.0f;
    const float scale = 0.1f;  // Scale of the noise
    const float speed = 0.01f; // Speed of animation
    
    while (!interrupt_received) {
      for (int x = 0; x < canvas()->width(); ++x) {
        for (int y = 0; y < canvas()->height(); ++y) {
          // Generate noise value between 0 and 1
          float nx = x * scale;
          float ny = y * scale;
          float nz = z;
          
          // stb_perlin_noise3 returns values between -1 and 1
          float n = (stb_perlin_noise3(nx, ny, nz, 0, 0, 0, seed) + 1.0f) * 0.5f;
          
          // Convert to grayscale (0-255)
          uint8_t value = static_cast<uint8_t>(n * 255);
          
          // Set pixel color (grayscale)
          canvas()->SetPixel(x, y, value, value, value);
        }
      }
      
      // Increment z for animation
      z += speed;
      
      // Small delay to control animation speed
      usleep(50 * 1000);
    }
  }

private:
  Canvas *const canvas_;
  int seed;  // Random seed for noise
};

int main(int argc, char *argv[]) {
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

  PerlinNoiseGenerator *generator = new PerlinNoiseGenerator(matrix);
  generator->Run();
  
  delete generator;
  delete matrix;
  return 0;
} 