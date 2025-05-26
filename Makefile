RGB_LIBDIR=./lib
RGB_LIBRARY_NAME=rgbmatrix
RGB_LIBRARY=$(RGB_LIBDIR)/lib$(RGB_LIBRARY_NAME).a

all : $(RGB_LIBRARY)
	$(MAKE) -C custom-displays

$(RGB_LIBRARY): FORCE
	$(MAKE) -C $(RGB_LIBDIR)

clean:
	$(MAKE) -C lib clean
	$(MAKE) -C utils clean
	$(MAKE) -C custom-displays clean

FORCE:
.PHONY: FORCE
