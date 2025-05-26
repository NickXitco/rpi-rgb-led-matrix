#include "led-matrix.h"
#include "graphics.h"
#include "animation.h"

#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <chrono>

using namespace rgb_matrix;
using namespace std::chrono;

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
  interrupt_received = true;
}

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

  FrameCanvas *offscreen = matrix->CreateFrameCanvas();
  PerlinNoiseAnimation animation(offscreen);
  
  auto last_frame = high_resolution_clock::now();
  
  while (!interrupt_received) {
    auto now = high_resolution_clock::now();
    float delta_time = duration<float>(now - last_frame).count();
    last_frame = now;

    animation.Update(delta_time);
    animation.Draw();
    
    offscreen = matrix->SwapOnVSync(offscreen);
    animation.SetCanvas(offscreen);
  }
  
  delete matrix;
  return 0;
} 