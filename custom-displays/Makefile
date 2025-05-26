CFLAGS=-Wall -O3 -g -Wextra -Wno-unused-parameter
CXXFLAGS=$(CFLAGS)

# ImageMagick configuration
MAGICK_CXXFLAGS := $(shell pkg-config --cflags Magick++)
MAGICK_LDFLAGS := $(shell pkg-config --libs Magick++)

# Define MAGICKCORE_QUANTUM_DEPTH and MAGICKCORE_HDRI_ENABLE
MAGICK_DEFINES := -DMAGICKCORE_QUANTUM_DEPTH=16 -DMAGICKCORE_HDRI_ENABLE=0

CXXFLAGS += -std=c++17 $(MAGICK_CXXFLAGS) $(MAGICK_DEFINES)
LDFLAGS += $(MAGICK_LDFLAGS) -lcurl -lssl -lcrypto

# Add external directory to include path
CXXFLAGS += -Iexternal
 
OBJECTS=hello-world.o ip-display.o weather-display.o perlin-noise.o
BINARIES=hello-world ip-display weather-display perlin-noise

# Where our library resides. You mostly only need to change the
# RGB_LIB_DISTRIBUTION, this is where the library is checked out.
RGB_LIB_DISTRIBUTION=..
RGB_INCDIR=$(RGB_LIB_DISTRIBUTION)/include
RGB_LIBDIR=$(RGB_LIB_DISTRIBUTION)/lib
RGB_LIBRARY_NAME=rgbmatrix
RGB_LIBRARY=$(RGB_LIBDIR)/lib$(RGB_LIBRARY_NAME).a
LDFLAGS+=-L$(RGB_LIBDIR) -l$(RGB_LIBRARY_NAME) -lrt -lm -lpthread -lcurl -lMagick++-6.Q16

# Additional include paths for nlohmann/json and ImageMagick
CXXFLAGS+=-I/usr/include/ImageMagick-6 -I/usr/include/arm-linux-gnueabihf/ImageMagick-6

all : $(BINARIES)

$(RGB_LIBRARY): FORCE
	$(MAKE) -C $(RGB_LIBDIR)

hello-world : hello-world.o $(RGB_LIBRARY)
	$(CXX) $< -o $@ $(LDFLAGS)

ip-display : ip-display.o $(RGB_LIBRARY)
	$(CXX) $< -o $@ $(LDFLAGS)

weather-display : weather-display.o $(RGB_LIBRARY)
	$(CXX) $< -o $@ $(LDFLAGS)

perlin-noise : perlin-noise.o $(RGB_LIBRARY)
	$(CXX) $< -o $@ $(LDFLAGS)

%.o : %.cc
	$(CXX) -I$(RGB_INCDIR) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(BINARIES)

FORCE:
.PHONY: FORCE
