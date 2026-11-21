/*
 * Raspberry Pi LED Matrix Clock
 *
 * Copyright (C) 2026 Ksyear
 *
 * This project is a derivative work based on the rpi-rgb-led-matrix library
 * (https://github.com/hzeller/rpi-rgb-led-matrix), original code by Henner Zeller.
 *
 * Modified by Ksyear on 2026-01-05:
 * - Created clock.cpp to implement digital clock functionality.
 * - Restructured directories and filtered necessary library files.
 *
 * Modified by Ksyear on 2026-01-19:
 * - Added logic to change clock color based on the day of the week (Weekday, Saturday, Sunday).
 *
 * Modified by Ksyear on 2026-11-21:
 * - Added bus arrival display (bus.cpp, busfetch.py) with four display modes
 *   selected by the `clockmode` command (settings.cpp, screen*.cpp).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include "led-matrix.h"
#include "graphics.h"
#include "bus.h"
#include "layout_common.h"
#include "screen.h"
#include "settings.h"
#include <iostream>
#include <time.h>
#include <ctime>
#include <signal.h>

using namespace rgb_matrix;

volatile bool interrupt_received = false;

static void InterruptHandler(int /*signo*/) {
        interrupt_received = true;
}

int main(int argc, char *argv[]) {
    RGBMatrix::Options defaults;
    defaults.rows = 16;
    defaults.cols = 32;
    defaults.chain_length = 3;
    defaults.parallel = 1;
    defaults.hardware_mapping = "regular";
    defaults.brightness = 50;

    RuntimeOptions runtime_defaults;
    runtime_defaults.gpio_slowdown = 4;
    //RGBMatrix *matrix = RGBMatrix::CreateFromOptions(defaults, runtime_defaults, &argc, &argv);
    if (!rgb_matrix::ParseOptionsFromFlags(&argc, &argv, &defaults, &runtime_defaults)) {
            return 1;
    }
    RGBMatrix *matrix = RGBMatrix::CreateFromOptions(defaults, runtime_defaults);

    if (matrix == NULL) return 1;

    Font small_font, large_font;
    if (!small_font.LoadFont("./fonts/5x8.bdf") || !large_font.LoadFont("./fonts/8x13B.bdf")) {
        std::cerr << "Font load failed." << std::endl;
        return 1;
    }
    const ClockFonts fonts = { &small_font, &large_font };

    signal(SIGINT, InterruptHandler);
    signal(SIGTERM, InterruptHandler);

    // Display mode (0 off, 1 alternate, 2 rotate, 3 fixed) comes from ./display.conf,
    // written by `./clockmode`; it is re-read about once a second.
    SettingsFile settings("./display.conf");
    settings.Poll(MonotonicSeconds());

    // Bus arrivals are fetched by ./busfetch.py in a background thread, so the
    // network never blocks this loop. Without ./bus.conf the screen stays the
    // classic clock. Relative paths are used on purpose: after CreateFromOptions()
    // the process runs as `daemon`, which can only reach the working directory.
    // (The feed is intentionally never deleted; its thread may outlive main().)
    BusFeed *bus = new BusFeed("exec timeout -k 5 20 python3 -I ./busfetch.py", "./bus.conf");
    bus->SetEnabled(settings.Get().mode != DisplayMode::kOff);
    bus->Start();
    ClockScreen screen;

    FrameCanvas *canvas = matrix->CreateFrameCanvas();

    struct timespec sleep_time;
    sleep_time.tv_sec = 0;
    sleep_time.tv_nsec = 100000000L;

    while (!interrupt_received) {
        time_t now = time(0);
        struct tm *ltm = localtime(&now);

        const double mono = MonotonicSeconds();
        if (settings.Poll(mono)) {
            bus->SetEnabled(settings.Get().mode != DisplayMode::kOff);
        }

        canvas->Clear();
        screen.Draw(canvas, fonts, *ltm, bus->Snapshot(), settings.Get(), mono);
        canvas = matrix->SwapOnVSync(canvas);

        nanosleep(&sleep_time, NULL);
    }

    bus->Stop();
    matrix->Clear();
    delete matrix;
    return 0;
}
