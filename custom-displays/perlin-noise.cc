#include "led-matrix.h"
#include "graphics.h"
#include "FastNoise2.h"

#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <random>
#include <vector>

using namespace rgb_matrix;

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
  interrupt_received = true;
}

class PerlinNoiseGenerator {
protected:
  PerlinNoiseGenerator(Canvas *canvas) : canvas_(canvas) {
    // Create noise generator
    noise = FastNoise2::New<FastNoise2::Perlin>();
    
    // Configure noise settings
    noise->SetSeed(std::random_device()());
    noise->SetFrequency(0.1f);
  }

  inline Canvas *canvas() { return canvas_; }

public:
  virtual ~PerlinNoiseGenerator() {}
  
  void Run() {
    float z = 0.0f;
    const float speed = 0.01f;
    
    // Create a buffer for noise values
    std::vector<float> noiseBuffer(canvas()->width() * canvas()->height());
    
    while (!interrupt_received) {
      // Generate noise for the entire frame at once
      for (int y = 0; y < canvas()->height(); ++y) {
        for (int x = 0; x < canvas()->width(); ++x) {
          float nx = x * 0.1f;
          float ny = y * 0.1f;
          float nz = z;
          
          // Get noise value (-1 to 1) and convert to 0-1 range
          float n = (noise->GenSingle2D(nx, ny, nz) + 1.0f) * 0.5f;
          
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
  std::unique_ptr<FastNoise2::Perlin> noise;
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