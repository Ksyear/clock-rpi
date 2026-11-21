/*
 * Raspberry Pi LED Matrix Clock - text preview (development tool)
 *
 * Copyright (C) 2026 Ksyear
 *
 * Renders the 96x16 clock screen as text so layouts can be checked on any
 * computer, without the LED matrix or root.
 *
 *   ./preview        every bus display state, with a fixed date and time
 *   ./preview -      the output of busfetch.py read from stdin, current time
 *                    e.g. python3 -I busfetch.py | ./preview -
 *
 * Pixel symbols: W white, R red, G green, B blue, C route (sky blue),
 * O minutes (orange), o stale minutes, g "--" grey, r error red.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include "bus.h"
#include "layout.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <iostream>
#include <iterator>
#include <string>

namespace {

const int kWidth = 96;
const int kHeight = 16;

class TextCanvas : public rgb_matrix::Canvas {
public:
    TextCanvas() { Clear(); }
    int width() const override { return kWidth; }
    int height() const override { return kHeight; }
    void SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) override {
        if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) {
            ++clipped_;
            return;
        }
        px_[y][x] = Symbol(r, g, b);
    }
    void Clear() override {
        memset(px_, '.', sizeof(px_));
        clipped_ = 0;
    }
    void Fill(uint8_t r, uint8_t g, uint8_t b) override {
        memset(px_, Symbol(r, g, b), sizeof(px_));
    }
    int clipped() const { return clipped_; }

    void Print() const {
        printf("    ");
        for (int x = 0; x < kWidth; x++) putchar(x % 10 == 0 ? '0' + (x / 10) % 10 : ' ');
        putchar('\n');
        for (int y = 0; y < kHeight; y++) {
            printf("%2d  %.*s\n", y, kWidth, px_[y]);
        }
    }

private:
    static char Symbol(uint8_t r, uint8_t g, uint8_t b) {
        struct { uint8_t r, g, b; char c; } table[] = {
            {0, 0, 0, '.'},       {255, 255, 255, 'W'}, {255, 0, 0, 'R'},
            {0, 255, 0, 'G'},     {0, 0, 255, 'B'},     {0, 180, 255, 'C'},
            {255, 160, 0, 'O'},   {110, 70, 0, 'o'},    {90, 90, 90, 'g'},
            {160, 0, 0, 'r'},
        };
        for (const auto &e : table) {
            if (e.r == r && e.g == g && e.b == b) return e.c;
        }
        return '#';
    }

    char px_[kHeight][kWidth];
    int clipped_ = 0;
};

struct Fonts {
    rgb_matrix::Font small, large;
    ClockFonts Get() const { return ClockFonts{ &small, &large }; }
};

int Render(const Fonts &fonts, const char *title, const struct tm &t,
           const BusSnapshot &snap, double now, bool blink_on) {
    const BusView view = ComputeBusView(snap, now);
    TextCanvas canvas;
    DrawClockScreen(&canvas, fonts.Get(), t, view, blink_on);
    printf("== %s  ->  [%s] [%s]\n", title, view.route.c_str(), view.text.c_str());
    canvas.Print();
    if (canvas.clipped() > 0) {
        printf("!! %d pixel(s) fell outside the 96x16 screen\n", canvas.clipped());
        return 1;
    }
    return 0;
}

BusSnapshot Data(double age, std::vector<BusArrival> buses, double now) {
    BusSnapshot s;
    s.status = BusStatus::kOk;
    s.has_data = true;
    s.fetched_at = now - age;
    s.buses = buses;
    s.label = "102";
    return s;
}

}  // namespace

int main(int argc, char *argv[]) {
    Fonts fonts;
    if (!fonts.small.LoadFont("./fonts/5x8.bdf") || !fonts.large.LoadFont("./fonts/8x13B.bdf")) {
        fprintf(stderr, "Font load failed (run from the project directory).\n");
        return 1;
    }

    if (argc == 2 && strcmp(argv[1], "-") == 0) {
        std::string out((std::istreambuf_iterator<char>(std::cin)),
                        std::istreambuf_iterator<char>());
        const double now = MonotonicSeconds();
        BusSnapshot snap;
        if (!ParseFetchOutput(out, now, &snap)) {
            fprintf(stderr, "Input does not follow the busfetch.py protocol.\n");
            return 1;
        }
        time_t t = time(NULL);
        return Render(fonts, "stdin", *localtime(&t), snap, now, true);
    }

    // Saturday 2026-11-21 14:05:24, and a weekday / a Sunday for the colors.
    struct tm sat = {};
    sat.tm_year = 2026 - 1900; sat.tm_mon = 10; sat.tm_mday = 21;
    sat.tm_hour = 14; sat.tm_min = 5; sat.tm_sec = 24;
    sat.tm_wday = 6;
    struct tm mon = sat; mon.tm_mday = 23; mon.tm_wday = 1;
    struct tm sun = sat; sun.tm_mday = 22; sun.tm_wday = 0;

    const double now = 10000;
    BusSnapshot none;  // no bus.conf
    BusSnapshot auth;
    auth.status = BusStatus::kAuthError;
    auth.label = "102";

    int bad = 0;
    bad += Render(fonts, "no bus.conf (classic clock)", sat, none, now, true);
    bad += Render(fonts, "waiting at the terminal, 23 min", mon, Data(0, {{"102", 1409, 2}}, now), now, true);
    bad += Render(fonts, "3 min, 4 stops", sun, Data(0, {{"102", 200, 4}}, now), now, true);
    bad += Render(fonts, "12 min, data 200 s old (dimmed)", sat, Data(200, {{"102", 920, 6}}, now), now, true);
    bad += Render(fonts, "99 min cap", sat, Data(0, {{"102", 9000, 40}}, now), now, true);
    bad += Render(fonts, "1 stop left: SOON (blink on)", sat, Data(0, {{"102", 150, 1}}, now), now, true);
    bad += Render(fonts, "1 stop left: SOON (blink off)", sat, Data(0, {{"102", 150, 1}}, now), now, false);
    bad += Render(fonts, "bus passed, waiting for new data", sat, Data(60, {{"102", 10, 1}}, now), now, true);
    bad += Render(fonts, "no bus in service (classic clock)", sat, Data(0, {}, now), now, true);
    bad += Render(fonts, "key not registered", sat, auth, now, true);
    bad += Render(fonts, "data older than 300 s (classic clock)", sat, Data(301, {{"102", 900, 5}}, now), now, true);
    return bad == 0 ? 0 : 1;
}
