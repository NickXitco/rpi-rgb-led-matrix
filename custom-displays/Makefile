# Custom RGB Matrix Display Programs
# This Makefile is for compiling custom display programs

# The top-level Makefile sets up the compiler options and necessary libraries
include ../Makefile

# Where to find the header files
INCLUDES=-I../include

# What programs to build
TARGETS=hello-world

# All the object files
OBJECTS=$(TARGETS:%=%.o)

# The default target
all : $(TARGETS)

# How to build the programs
% : %.o
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

# How to build the object files
%.o : %.cc
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# Clean up
clean:
	rm -f $(TARGETS) $(OBJECTS)

.PHONY: all clean 