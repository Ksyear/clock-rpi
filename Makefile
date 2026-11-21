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

# Screens: screen.cpp picks the mode; one file per mode.
SCREEN_SRCS=screen.cpp screen_alternate.cpp screen_rotate.cpp screen_fixed.cpp
COMMON_SRCS=bus.cpp settings.cpp layout_common.cpp $(SCREEN_SRCS)
CLOCK_SRCS=clock.cpp $(COMMON_SRCS)
HEADERS=bus.h settings.h layout_common.h screen.h

all: clock

clock: $(CLOCK_SRCS) $(HEADERS) $(RGB_LIBRARY)
	$(CXX) $(CXXFLAGS) $(CLOCK_SRCS) -o clock $(LDFLAGS)

# Development tool: draws the screen as text. Builds on Linux and macOS without
# the LED library (only the font and text drawing sources are compiled in).
PREVIEW_SRCS=preview.cpp $(COMMON_SRCS) $(RGB_LIBDIR)/bdf-font.cc $(RGB_LIBDIR)/graphics.cc

preview: $(PREVIEW_SRCS) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(PREVIEW_SRCS) -o preview -lpthread

# Runs the Python tests, checks that no frame of any mode draws off the panel or
# over itself, and compares the mode timelines with the expected ones.
check: preview
	python3 -I tests/test_busfetch.py
	python3 -I tests/test_clockmode.py
	python3 -I tests/test_repo.py
	./preview --feed-test
	./preview > /dev/null && echo "preview: every frame fits in 96x16"
	./preview --timeline | diff -u tests/timeline_expected.txt - && echo "preview: timelines as expected"

clean:
	rm -f clock preview

.PHONY: all clean check
