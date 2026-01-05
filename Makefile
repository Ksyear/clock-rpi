# Raspberry Pi LED Matrix Clock - Makefile
#
# This project is a derivative work based on the rpi-rgb-led-matrix library
# (https://github.com/hzeller/rpi-rgb-led-matrix), original code by Henner Zeller.

RGB_LIBDIR=./lib
RGB_INCDIR=./include
RGB_LIBRARY_NAME=rgbmatrix
RGB_LIBRARY=$(RGB_LIBDIR)/lib$(RGB_LIBRARY_NAME).a

CXX=g++
CXXFLAGS=-I$(RGB_INCDIR) -O3
LDFLAGS=-L$(RGB_LIBDIR) -l$(RGB_LIBRARY_NAME) -lrt -lm -lpthread

all: clock

clock: clock.cpp $(RGB_LIBRARY)
	$(CXX) $(CXXFLAGS) clock.cpp -o clock $(LDFLAGS)

clean:
	rm -f clock

.PHONY: all clean
