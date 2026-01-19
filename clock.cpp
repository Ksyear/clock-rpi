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
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include "led-matrix.h"
#include "graphics.h"
#include <iostream>
#include <time.h>
#include <ctime>
#include <signal.h>
#include <algorithm>
#include <string>

using namespace rgb_matrix;

volatile bool interrupt_received = false;

static void InterruptHandler(int signo) {
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
		
    Color color1(255, 0, 0);
    Color color2(0, 255, 0);
    Color color3(0, 0, 255);
    Color color4(255, 255, 255);

    signal(SIGINT, InterruptHandler);
    signal(SIGTERM, InterruptHandler);

    FrameCanvas *canvas = matrix->CreateFrameCanvas();
    char date_buf[12], week_buf[8], time_buf[12];

    struct timespec sleep_time;
    sleep_time.tv_sec = 0;
    sleep_time.tv_nsec = 100000000L;

    while (!interrupt_received) {
        time_t now = time(0);
        struct tm *ltm = localtime(&now);

        strftime(date_buf, sizeof(date_buf), "%m-%d", ltm);
        strftime(week_buf, sizeof(week_buf), "%a", ltm);
        strftime(time_buf, sizeof(time_buf), "%H:%M:%S", ltm);

        for (int i = 0; week_buf[i]; i++) {
                week_buf[i] = toupper(week_buf[i]);
        }

        canvas->Clear();

        std::string week = week_buf;

        if(week.compare("SUN") == 0){
                rgb_matrix::DrawText(canvas, small_font, 3, 7, color1, NULL, date_buf, 0);
                rgb_matrix::DrawText(canvas, small_font, 8, 15, color1, NULL, week_buf, 0);
        } else if(week.compare("SAT") == 0){
                rgb_matrix::DrawText(canvas, small_font, 3, 7, color3, NULL, date_buf, 0);
                rgb_matrix::DrawText(canvas, small_font, 8, 15, color3, NULL, week_buf, 0);
        }else{
                rgb_matrix::DrawText(canvas, small_font, 3, 7, color2, NULL, date_buf, 0);
                rgb_matrix::DrawText(canvas, small_font, 8, 15, color2, NULL, week_buf, 0);
        }


        rgb_matrix::DrawText(canvas, large_font, 30, 13, color2, NULL, time_buf, 0);

        canvas = matrix->SwapOnVSync(canvas);

        nanosleep(&sleep_time, NULL);
    }

    matrix->Clear();
    delete matrix;
    return 0;
}
