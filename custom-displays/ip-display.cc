#include "led-matrix.h"
#include "graphics.h"

#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sstream>
#include <string>

using namespace rgb_matrix;

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
  interrupt_received = true;
}

// Function to get the first non-localhost IP address
std::string GetIPAddress() {
    struct ifaddrs *ifaddr, *ifa;
    std::string ip = "No IP";
    
    if (getifaddrs(&ifaddr) == -1) {
        return "Error";
    }

    // Walk through linked list of interfaces
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        // Check if it's an IPv4 address
        if (ifa->ifa_addr->sa_family == AF_INET) {
            // Skip localhost
            if (strcmp(ifa->ifa_name, "lo") == 0)
                continue;

            char ipstr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr,
                     ipstr, INET_ADDRSTRLEN);
            ip = ipstr;
            break;
        }
    }

    freeifaddrs(ifaddr);
    return ip;
}

int main(int argc, char *argv[]) {
  RGBMatrix::Options defaults;
  defaults.hardware_mapping = "regular";
  defaults.rows = 32;
  defaults.cols = 64;
  defaults.chain_length = 1;
  defaults.parallel = 1;
  defaults.show_refresh_rate = true;
  defaults.panel_type = "FM6127";

  // Set up signal handler for clean exit
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
  if (!font.LoadFont("../fonts/5x7.bdf")) {
    fprintf(stderr, "Couldn't load font\n");
    return 1; 
  }

  // Colors
  Color ip_color(0, 255, 0);    // Green for IP

  while (!interrupt_received) {
    // Clear the offscreen canvas
    offscreen->Clear();

    // Get current IP
    std::string ip = GetIPAddress();
    
    int y_pos = 7;
    std::istringstream ss(ip);
    std::string segment;
    while(std::getline(ss, segment, '.')) {
        rgb_matrix::DrawText(offscreen, font, 1, y_pos, ip_color, segment.c_str());
        y_pos += 8;  // Move down for next line
    }

    // Swap the offscreen canvas with the onscreen one
    offscreen = matrix->SwapOnVSync(offscreen);

    // Update every 2 seconds
    sleep(2);
  }

  // Clean up
  delete matrix;
  return 0;
} 