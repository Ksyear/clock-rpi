# Raspberry Pi LED Matrix Clock - Makefile
#
# This project is a derivative work based on the rpi-rgb-led-matrix library
# (https://github.com/hzeller/rpi-rgb-led-matrix), original code by Henner Zeller.

RGB_LIBDIR=./lib
RGB_INCDIR=./include
RGB_LIBRARY_NAME=rgbmatrix
RGB_LIBRARY=$(RGB_LIBDIR)/lib$(RGB_LIBRARY_NAME).a

CXX=g++
CXXFLAGS=-I$(RGB_INCDIR) -O3 -std=c++17 -Wall
LDFLAGS=-L$(RGB_LIBDIR) -l$(RGB_LIBRARY_NAME) -lrt -lm -lpthread

CLOCK_SRCS=clock.cpp bus.cpp layout.cpp
HEADERS=bus.h layout.h

all: clock

clock: $(CLOCK_SRCS) $(HEADERS) $(RGB_LIBRARY)
	$(CXX) $(CXXFLAGS) $(CLOCK_SRCS) -o clock $(LDFLAGS)

# Development tool: draws the screen as text. Builds on Linux and macOS without
# the LED library (only the font and text drawing sources are compiled in).
PREVIEW_SRCS=preview.cpp bus.cpp layout.cpp $(RGB_LIBDIR)/bdf-font.cc $(RGB_LIBDIR)/graphics.cc

preview: $(PREVIEW_SRCS) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(PREVIEW_SRCS) -o preview -lpthread

# Runs the parser tests and checks that no screen state draws off the panel.
check: preview
	python3 -I tests/test_busfetch.py
	./preview > /dev/null && echo "preview: all screens fit in 96x16"

clean:
	rm -f clock preview

.PHONY: all clean check
