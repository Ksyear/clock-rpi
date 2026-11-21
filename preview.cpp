/*
 * Raspberry Pi LED Matrix Clock - text preview (development tool)
 *
 * Copyright (C) 2026 Ksyear
 *
 * Renders the 96x16 clock screen as text so every display mode can be checked
 * on any computer, without the LED matrix or root.
 *
 *   ./preview                    still frames of every mode and state, then
 *                                60-second timelines (a frame per change)
 *   ./preview --timeline         timelines as text only (make check compares
 *                                it with tests/timeline_expected.txt)
 *   ./preview - [MODE] [SECS]    busfetch.py output from stdin, shown in MODE
 *                                (0-3 or a name; default ./display.conf) for SECS
 *                                e.g. python3 -I busfetch.py | ./preview - 1 30
 *   ./preview --settings FILE    how the clock reads a display.conf
 *
 * Exits with 1 if any frame draws outside the 96x16 panel or draws over an
 * already lit pixel.
 *
 * Pixel symbols: W white, R red, G green, B blue, C route (sky blue),
 * O minutes (orange), o stale minutes, g grey, r error red.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include "bus.h"
#include "layout_common.h"
#include "screen.h"
#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

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
        const char c = Symbol(r, g, b);
        if (c != '.' && px_[y][x] != '.') ++overlaps_;
        px_[y][x] = c;
    }
    void Clear() override {
        memset(px_, '.', sizeof(px_));
        clipped_ = overlaps_ = 0;
    }
    void Fill(uint8_t r, uint8_t g, uint8_t b) override {
        memset(px_, Symbol(r, g, b), sizeof(px_));
    }
    int problems() const { return clipped_ + overlaps_; }
    int clipped() const { return clipped_; }
    int overlaps() const { return overlaps_; }

    void Print() const {
        printf("    ");
        for (int x = 0; x < kWidth; x++) putchar(x % 10 == 0 ? '0' + (x / 10) % 10 : ' ');
        putchar('\n');
        for (int y = 0; y < kHeight; y++) printf("%2d  %.*s\n", y, kWidth, px_[y]);
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
    int overlaps_ = 0;
};

struct Fonts {
    rgb_matrix::Font small, large;
    ClockFonts Get() const { return ClockFonts{ &small, &large }; }
};

// A fixed calendar time plus `secs` (2026-11-21 is a Saturday).
struct tm ClockAt(int mday, int wday, int secs) {
    struct tm t = {};
    t.tm_year = 2026 - 1900;
    t.tm_mon = 10;
    t.tm_mday = mday;
    t.tm_wday = wday;
    const int total = 14 * 3600 + 5 * 60 + 24 + secs;
    t.tm_hour = (total / 3600) % 24;
    t.tm_min = (total / 60) % 60;
    t.tm_sec = total % 60;
    return t;
}

const double kT0 = 10000;  // fake MonotonicSeconds() at the start of a scenario
int g_problems = 0;

void Report(const TextCanvas &canvas) {
    if (canvas.problems() == 0) return;
    printf("!! %d pixel(s) outside the panel, %d pixel(s) drawn twice\n",
           canvas.clipped(), canvas.overlaps());
    g_problems += canvas.problems();
}

BusSnapshot Data(double age, const std::vector<BusArrival> &buses) {
    BusSnapshot s;
    s.status = BusStatus::kOk;
    s.has_data = true;
    s.fetched_at = kT0 - age;
    s.buses = buses;
    return s;
}

BusSnapshot ErrorSnap(BusStatus status) {
    BusSnapshot s;
    s.status = status;
    return s;
}

DisplaySettings Mode(DisplayMode m) {
    DisplaySettings s;
    s.mode = m;
    return s;
}

// Values from tests/fixtures/daejeon_many.xml (2026-11-21 14:05).
const std::vector<BusArrival> kStop = {
    {"113", 359, 3}, {"102", 1409, 2}, {"117", 1418, 5},
    {"104", 1420, 2}, {"11", 1610, 2}, {"212", 2147, 24},
};

// --- still frames -----------------------------------------------------------

void Still(const Fonts &fonts, const char *title, const BusSnapshot &snap,
           const DisplaySettings &s, double at = 0, int mday = 21, int wday = 6) {
    ClockScreen screen;
    TextCanvas canvas;
    const struct tm t = ClockAt(mday, wday, 0);
    std::string tag = screen.Draw(&canvas, fonts.Get(), t, snap, s, kT0 + at);
    printf("== [%s] %s  ->  %s\n", ModeName(s.mode), title, tag.c_str());
    canvas.Print();
    Report(canvas);
}

// Mode 1 bus page right after the pages start (`blink_off` 0.6 s later).
void StillBusPage(const Fonts &fonts, const char *title, const BusSnapshot &snap,
                  double page_offset, bool blink_off) {
    AlternateScreen alt;
    const DisplaySettings s = Mode(DisplayMode::kAlternate);
    const BusBoard board = ComputeBusBoard(snap, kT0);
    TextCanvas canvas;
    const struct tm t = ClockAt(21, 6, 0);
    alt.Reset(kT0, true);
    std::string tag = alt.Draw(&canvas, fonts.Get(), t, board, s, kT0);
    const double at = kT0 + page_offset + (blink_off ? 0.6 : 0);
    if (at != kT0) {
        canvas.Clear();
        tag = alt.Draw(&canvas, fonts.Get(), t, ComputeBusBoard(snap, at), s, at);
    }
    printf("== [alternate] %s  ->  %s\n", title, tag.c_str());
    canvas.Print();
    Report(canvas);
}

void Stills(const Fonts &fonts) {
    const std::vector<BusArrival> eight = {
        {"113", 359, 3}, {"102", 1409, 2}, {"117", 1418, 5}, {"104", 1420, 2},
        {"11", 1610, 2}, {"212", 2147, 24}, {"1002", 480, 6}, {"B1", 700, 9},
    };
    std::vector<BusArrival> soon = kStop;
    soon[0] = {"113", 50, 1};

    // mode 1
    StillBusPage(fonts, "bus page, 6 routes", Data(0, kStop), 0, false);
    StillBusPage(fonts, "bus page, 113 SOON (blink on)", Data(0, soon), 0, false);
    StillBusPage(fonts, "bus page, 113 SOON (blink off)", Data(0, soon), 0, true);
    StillBusPage(fonts, "bus page, data 200 s old (dimmed)", Data(200, kStop), 0, false);
    StillBusPage(fonts, "8 routes, page 1/2", Data(0, eight), 0, false);
    StillBusPage(fonts, "8 routes, page 2/2", Data(0, eight), 5, false);
    StillBusPage(fonts, "key error page", ErrorSnap(BusStatus::kAuthError), 0, false);
    StillBusPage(fonts, "daily limit page", ErrorSnap(BusStatus::kLimitError), 0, false);
    Still(fonts, "clock part / no bus in service", Data(0, {}), Mode(DisplayMode::kAlternate));

    // mode 2
    Still(fonts, "first route (11)", Data(0, kStop), Mode(DisplayMode::kRotate));
    Still(fonts, "113 SOON (blink on)", Data(0, {{"113", 50, 1}}), Mode(DisplayMode::kRotate));
    Still(fonts, "212 data 200 s old (dimmed)", Data(200, {{"212", 2147, 24}}), Mode(DisplayMode::kRotate));
    Still(fonts, "4-character route, 99 min", Data(0, {{"1002", 9000, 40}}), Mode(DisplayMode::kRotate));
    Still(fonts, "key error", ErrorSnap(BusStatus::kAuthError), Mode(DisplayMode::kRotate));
    Still(fonts, "no bus in service (classic clock)", Data(0, {}), Mode(DisplayMode::kRotate));

    // mode 3 (the earlier single-route screen)
    const DisplaySettings fixed = Mode(DisplayMode::kFixed);
    Still(fonts, "no bus.conf (classic clock)", BusSnapshot(), fixed);
    Still(fonts, "waiting at the terminal, 23 min", Data(0, {{"102", 1409, 2}}), fixed, 0, 23, 1);
    Still(fonts, "3 min, 4 stops", Data(0, {{"102", 200, 4}}), fixed, 0, 22, 0);
    Still(fonts, "12 min, data 200 s old (dimmed)", Data(200, {{"102", 920, 6}}), fixed);
    Still(fonts, "99 min cap", Data(0, {{"102", 9000, 40}}), fixed);
    Still(fonts, "1 stop left: SOON (blink on)", Data(0, {{"102", 150, 1}}), fixed);
    Still(fonts, "1 stop left: SOON (blink off)", Data(0, {{"102", 150, 1}}), fixed, 0.5);
    Still(fonts, "bus passed, waiting for new data", Data(60, {{"102", 10, 1}}), fixed);
    Still(fonts, "102 not at the stop now (classic clock)", Data(0, {{"104", 300, 3}}), fixed);
    Still(fonts, "key not registered", ErrorSnap(BusStatus::kAuthError), fixed);
    Still(fonts, "data older than 300 s (classic clock)", Data(301, {{"102", 900, 5}}), fixed);

    // mode 0
    Still(fonts, "original clock only", Data(0, kStop), Mode(DisplayMode::kOff));
}

// --- timelines --------------------------------------------------------------

struct Switch {
    double at;
    DisplayMode mode;
};

// Runs a scenario in 0.1 s frames; prints a line (and the frame, unless
// `text_only`) whenever the screen's tag changes. Every frame is checked.
void Timeline(const Fonts &fonts, bool text_only, const char *title, const BusSnapshot &snap,
              DisplayMode mode, double secs, const std::vector<Switch> &switches = {}) {
    printf("== %s\n", title);
    ClockScreen screen;
    DisplaySettings s = Mode(mode);
    std::string last;
    const int frames = static_cast<int>(secs * 10 + 0.5);
    for (int i = 0; i <= frames; i++) {
        const double t = i / 10.0;
        for (const Switch &sw : switches) {
            if (i == static_cast<int>(sw.at * 10 + 0.5)) s.mode = sw.mode;
        }
        TextCanvas canvas;
        const std::string tag =
            screen.Draw(&canvas, fonts.Get(), ClockAt(21, 6, i / 10), snap, s, kT0 + t);
        if (canvas.problems() > 0) {
            printf("!! t=%.1f\n", t);
            Report(canvas);
        }
        if (tag != last) {
            printf("%5.1f [%s] %s\n", t, ModeName(s.mode), tag.c_str());
            if (!text_only) canvas.Print();
            last = tag;
        }
    }
}

void Timelines(const Fonts &fonts, bool text_only) {
    const std::vector<BusArrival> eight = {
        {"113", 359, 3}, {"102", 1409, 2}, {"117", 1418, 5}, {"104", 1420, 2},
        {"11", 1610, 2}, {"212", 2147, 24}, {"1002", 480, 6}, {"B1", 700, 9},
    };
    std::vector<BusArrival> passing = kStop;
    passing[0] = {"113", 7, 1};  // passes 30 s after its estimate: t = 37 s

    Timeline(fonts, text_only, "mode 1, 6 routes, from boot", Data(0, kStop),
             DisplayMode::kAlternate, 60);
    Timeline(fonts, text_only, "mode 1, 8 routes (2 pages)", Data(0, eight),
             DisplayMode::kAlternate, 45);
    Timeline(fonts, text_only, "mode 1, 113 passes at 37 s while its page is up",
             Data(0, passing), DisplayMode::kAlternate, 60);
    Timeline(fonts, text_only, "mode 1, key error", ErrorSnap(BusStatus::kAuthError),
             DisplayMode::kAlternate, 25);
    Timeline(fonts, text_only, "mode 2, 6 routes", Data(0, kStop), DisplayMode::kRotate, 20);
    Timeline(fonts, text_only, "mode 2, 102 passes at 31 s while it is on screen",
             Data(0, {{"11", 1610, 2}, {"102", 1, 1}, {"104", 1420, 2}}),
             DisplayMode::kRotate, 40);
    Timeline(fonts, text_only, "mode 3, 102 counts down", Data(0, {{"102", 200, 4}}),
             DisplayMode::kFixed, 160);
    Timeline(fonts, text_only, "switches: 2 -> 1 at 7 s -> 3 at 12 s -> 0 at 15 s",
             Data(0, kStop), DisplayMode::kRotate, 18,
             {{7, DisplayMode::kAlternate}, {12, DisplayMode::kFixed}, {15, DisplayMode::kOff}});

    // A bus waiting at its terminal keeps the same estimate between fetches.
    printf("== holding: same estimate on the next fetch stops the countdown\n");
    BusSnapshot snap;
    ParseFetchOutput("OK 1\nLABEL BUS\nBUS 102 1409 2\n", kT0, &snap);
    printf("  t=0   %s\n", MinutesText(ComputeBusBoard(snap, kT0).entries[0]).c_str());
    printf("  t=89  %s (counting down)\n",
           MinutesText(ComputeBusBoard(snap, kT0 + 89).entries[0]).c_str());
    ParseFetchOutput("OK 1\nLABEL BUS\nBUS 102 1409 2\n", kT0 + 90, &snap);
    printf("  t=90  %s holding=%d\n",
           MinutesText(ComputeBusBoard(snap, kT0 + 90).entries[0]).c_str(), snap.buses[0].holding);
    printf("  t=179 %s (not counting down)\n",
           MinutesText(ComputeBusBoard(snap, kT0 + 179).entries[0]).c_str());
    ParseFetchOutput("OK 1\nLABEL BUS\nBUS 102 1300 2\n", kT0 + 180, &snap);
    printf("  t=180 %s holding=%d (departed)\n",
           MinutesText(ComputeBusBoard(snap, kT0 + 180).entries[0]).c_str(), snap.buses[0].holding);
}

bool ParseModeArg(const std::string &a, DisplayMode *m) {
    if (a == "0" || a == "off")            *m = DisplayMode::kOff;
    else if (a == "1" || a == "alternate") *m = DisplayMode::kAlternate;
    else if (a == "2" || a == "rotate")    *m = DisplayMode::kRotate;
    else if (a == "3" || a == "fixed")     *m = DisplayMode::kFixed;
    else return false;
    return true;
}

std::string ReadFile(const char *path) {
    std::ifstream f(path);
    std::stringstream text;
    if (f) text << f.rdbuf();
    return text.str();
}

}  // namespace

int main(int argc, char *argv[]) {
    if (argc == 3 && strcmp(argv[1], "--settings") == 0) {
        printf("%s\n", DumpDisplaySettings(ParseDisplaySettings(ReadFile(argv[2]))).c_str());
        return 0;
    }

    Fonts fonts;
    if (!fonts.small.LoadFont("./fonts/5x8.bdf") || !fonts.large.LoadFont("./fonts/8x13B.bdf")) {
        fprintf(stderr, "Font load failed (run from the project directory).\n");
        return 1;
    }

    if (argc >= 2 && strcmp(argv[1], "-") == 0) {
        std::string out((std::istreambuf_iterator<char>(std::cin)),
                        std::istreambuf_iterator<char>());
        BusSnapshot snap;
        if (!ParseFetchOutput(out, kT0, &snap)) {
            fprintf(stderr, "Input does not follow the busfetch.py protocol.\n");
            return 1;
        }
        DisplayMode mode = ParseDisplaySettings(ReadFile("./display.conf")).mode;
        if (argc >= 3 && !ParseModeArg(argv[2], &mode)) {
            fprintf(stderr, "MODE must be 0-3, off, alternate, rotate or fixed.\n");
            return 1;
        }
        const double secs = argc >= 4 ? atof(argv[3]) : 20;
        Timeline(fonts, false, "stdin", snap, mode, secs > 0 ? secs : 20);
        return g_problems == 0 ? 0 : 1;
    }

    const bool text_only = argc == 2 && strcmp(argv[1], "--timeline") == 0;
    if (!text_only) Stills(fonts);
    Timelines(fonts, text_only);
    return g_problems == 0 ? 0 : 1;
}
