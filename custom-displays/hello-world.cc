#include "led-matrix.h"
#include "graphics.h"

#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <signal.h>

using namespace rgb_matrix;

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
  interrupt_received = true;
}

int main(int argc, char *argv[]) {
  RGBMatrix::Options defaults;
  defaults.hardware_mapping = "regular";  // or e.g. "adafruit-hat"
  defaults.rows = 32;
  defaults.cols = 64;
  defaults.chain_length = 1;
  defaults.parallel = 1;
  defaults.show_refresh_rate = true;

  // It is always good to set up a signal handler to cleanly exit when we
  // receive a CTRL-C for instance.
  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);

  // Create the RGB matrix
  RGBMatrix *matrix = RGBMatrix::CreateFromFlags(&argc, &argv, &defaults);
  if (matrix == NULL)
    return 1;

  // Create a new canvas to be able to do double buffering
  FrameCanvas *offscreen = matrix->CreateFrameCanvas();

  // Create a font
  rgb_matrix::Font font;
  if (!font.LoadFont("../fonts/7x13.bdf")) {
    fprintf(stderr, "Couldn't load font\n");
    return 1;
  }

  // Create a color
  Color color(255, 255, 0);  // Yellow

  // Draw text
  rgb_matrix::DrawText(offscreen, font, 5, 20, color, "Hello World!");

  // Swap the offscreen canvas with the onscreen one
  offscreen = matrix->SwapOnVSync(offscreen);

  // Wait for CTRL-C
  while (!interrupt_received) {
    sleep(1);
  }

  // Clean up
  delete matrix;
  return 0;
} 