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
 *   ./preview --feed-test        runs the real BusFeed thread with a fake helper
 *                                and checks that pausing means zero helper runs
 *
 * Exits with 1 if any frame draws outside the 96x16 panel or draws over an
 * already lit pixel.
 *
 * Pixel symbols: W white, R red, G green, B blue, C route (sky blue),
 * O minutes (orange), o stale minutes, g grey, r error red,
 * P bus still at its terminal (lavender, timetable estimate).
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
            {160, 0, 0, 'r'},     {170, 120, 255, 'P'},
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

const int kDefaultSod = 14 * 3600 + 5 * 60 + 24;  // 14:05:24

// A fixed calendar day at `sod` seconds of day (2026-11-21 is a Saturday).
struct tm ClockAtSod(int mday, int wday, int sod) {
    struct tm t = {};
    t.tm_year = 2026 - 1900;
    t.tm_mon = 10;
    t.tm_mday = mday;
    t.tm_wday = wday;
    t.tm_hour = (sod / 3600) % 24;
    t.tm_min = (sod / 60) % 60;
    t.tm_sec = sod % 60;
    return t;
}

struct tm ClockAt(int mday, int wday, int secs) {
    return ClockAtSod(mday, wday, kDefaultSod + secs);
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

// The timings of the first version (3 s per route, every route, 5 s bus pages):
// the older scenarios use them, so their timeline lines must not change.
DisplaySettings Legacy(DisplayMode m) {
    DisplaySettings s = Mode(m);
    s.rotate_secs = 3;
    s.rotate_within = 99;
    s.rotate_min = 0;
    s.alt_bus_secs = 5;
    return s;
}

// Helper output as the feed would parse it at kT0 + at.
BusSnapshot Fetched(const std::string &out, double at = 0) {
    BusSnapshot s;
    ParseFetchOutput(out, kT0 + at, &s);
    return s;
}

// `python3 -I busfetch.py --parse-file daejeon tests/fixtures/daejeon_mixed.xml --now 15:12:58`
const char *kMixed =
    "OK 6\nLABEL BUS\nVIA daejeon\n"
    "BUS 102 482 2 WAIT 120\nBUS 113 520 4 RUN\nBUS 104 902 2 WAIT 120\n"
    "BUS 212 1226 10 RUN\nBUS 117 1562 5 WAIT 300\nBUS 11 3542 2 WAIT 120\n";

// Values from tests/fixtures/daejeon_many.xml (2026-11-21 14:05).
const std::vector<BusArrival> kStop = {
    {"113", 359, 3}, {"102", 1409, 2}, {"117", 1418, 5},
    {"104", 1420, 2}, {"11", 1610, 2}, {"212", 2147, 24},
};

// --- still frames -----------------------------------------------------------

void Still(const Fonts &fonts, const char *title, const BusSnapshot &snap,
           const DisplaySettings &s, double at = 0, int mday = 21, int wday = 6,
           int sod = kDefaultSod) {
    ClockScreen screen;
    TextCanvas canvas;
    const struct tm t = ClockAtSod(mday, wday, sod);
    std::string tag = screen.Draw(&canvas, fonts.Get(), t, snap, s, kT0 + at);
    printf("== [%s] %s  ->  %s\n", ModeName(s.mode), title, tag.c_str());
    canvas.Print();
    Report(canvas);
}

// Mode 1 bus page right after the pages start (`blink_off` 0.6 s later).
void StillBusPage(const Fonts &fonts, const char *title, const BusSnapshot &snap,
                  double page_offset, bool blink_off) {
    AlternateScreen alt;
    const DisplaySettings s = Legacy(DisplayMode::kAlternate);
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

    // buses still at their terminal (lavender), from a real response
    StillBusPage(fonts, "mixed: 102/104/117/11 at the terminal", Fetched(kMixed), 0, false);
    Still(fonts, "102 at the terminal, 8 min (lavender)", Fetched(kMixed), Mode(DisplayMode::kFixed));
    Still(fonts, "waiting bus past its departure stays at the drive time",
          Fetched("OK 1\nLABEL BUS\nBUS 102 200 2 WAIT 120\n"), Mode(DisplayMode::kFixed), 600);
    Still(fonts, "waiting bus 1 stop away does not blink",
          Fetched("OK 1\nLABEL BUS\nBUS 102 300 1 WAIT 60\n"), Mode(DisplayMode::kFixed), 0.6);
    Still(fonts, "nothing within 15 min: soonest bus, no rotation",
          Data(0, {{"212", 1200, 10}, {"117", 1500, 12}}), Mode(DisplayMode::kRotate));

    // quiet hours
    Still(fonts, "quiet hours 01:00-05:00 (02:00), fresh data", Data(0, kStop),
          Mode(DisplayMode::kRotate), 0, 21, 6, 2 * 3600);

    // mode 0
    Still(fonts, "original clock only", Data(0, kStop), Mode(DisplayMode::kOff));
}

// --- timelines --------------------------------------------------------------

struct Switch {
    double at;
    DisplayMode mode;
};

// New helper output arriving at `at` seconds, parsed like the feed does.
struct FeedAt {
    double at;
    std::string out;
};

// Runs a scenario in 0.1 s frames starting at `start_sod` (time of day); prints a
// line (and the frame, unless `text_only`) whenever the screen's tag changes.
// Every frame is checked for pixels outside the panel or drawn twice.
void Timeline(const Fonts &fonts, bool text_only, const char *title, BusSnapshot snap,
              DisplaySettings s, double secs, const std::vector<Switch> &switches = {},
              const std::vector<FeedAt> &feeds = {}, int start_sod = kDefaultSod) {
    printf("== %s\n", title);
    ClockScreen screen;
    std::string last;
    const int frames = static_cast<int>(secs * 10 + 0.5);
    for (int i = 0; i <= frames; i++) {
        const double t = i / 10.0;
        for (const Switch &sw : switches) {
            if (i == static_cast<int>(sw.at * 10 + 0.5)) s.mode = sw.mode;
        }
        for (const FeedAt &fa : feeds) {
            if (i == static_cast<int>(fa.at * 10 + 0.5)) ParseFetchOutput(fa.out, kT0 + t, &snap);
        }
        TextCanvas canvas;
        const std::string tag = screen.Draw(&canvas, fonts.Get(),
                                            ClockAtSod(21, 6, (start_sod + i / 10) % 86400),
                                            snap, s, kT0 + t);
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

    // Scenarios of the first version, with its timings (unchanged output).
    Timeline(fonts, text_only, "mode 1, 6 routes, from boot", Data(0, kStop),
             Legacy(DisplayMode::kAlternate), 60);
    Timeline(fonts, text_only, "mode 1, 8 routes (2 pages)", Data(0, eight),
             Legacy(DisplayMode::kAlternate), 45);
    Timeline(fonts, text_only, "mode 1, 113 passes at 37 s while its page is up",
             Data(0, passing), Legacy(DisplayMode::kAlternate), 60);
    Timeline(fonts, text_only, "mode 1, key error", ErrorSnap(BusStatus::kAuthError),
             Legacy(DisplayMode::kAlternate), 25);
    Timeline(fonts, text_only, "mode 2, 6 routes", Data(0, kStop), Legacy(DisplayMode::kRotate), 20);
    Timeline(fonts, text_only, "mode 2, 102 passes at 31 s while it is on screen",
             Data(0, {{"11", 1610, 2}, {"102", 1, 1}, {"104", 1420, 2}}),
             Legacy(DisplayMode::kRotate), 40);
    Timeline(fonts, text_only, "mode 3, 102 counts down", Data(0, {{"102", 200, 4}}),
             Legacy(DisplayMode::kFixed), 160);
    Timeline(fonts, text_only, "switches: 2 -> 1 at 7 s -> 3 at 12 s -> 0 at 15 s",
             Data(0, kStop), Legacy(DisplayMode::kRotate), 18,
             {{7, DisplayMode::kAlternate}, {12, DisplayMode::kFixed}, {15, DisplayMode::kOff}});

    // Buses at their terminal (real response, 15:12:58) with the current defaults.
    Timeline(fonts, text_only, "mode 2, real mix: 3..15 min are 102 104 113, 10 s each",
             Fetched(kMixed), Mode(DisplayMode::kRotate), 20);
    Timeline(fonts, text_only, "mode 1, real mix: 8 s bus page", Fetched(kMixed),
             Mode(DisplayMode::kAlternate), 25);
    Timeline(fonts, text_only, "mode 3, waiting 102 counts down to its drive time and stays",
             Fetched("OK 1\nLABEL BUS\nBUS 102 200 2 WAIT 120\n"), Mode(DisplayMode::kFixed), 160);
    Timeline(fonts, text_only, "mode 3, waiting 102 leaves, arrives, next one waits",
             Fetched("OK 1\nLABEL BUS\nBUS 102 482 2 WAIT 120\n"), Mode(DisplayMode::kFixed), 250, {},
             {{90, "OK 1\nLABEL BUS\nBUS 102 400 2 WAIT 120\n"},
              {180, "OK 2\nLABEL BUS\nBUS 102 29 1 RUN\nBUS 102 1100 2 WAIT 120\n"}});

    // Mode 2 rotation rules.
    Timeline(fonts, text_only, "mode 2, one route within 15 min: no rotation",
             Data(0, {{"212", 300, 5}, {"117", 1500, 12}}), Mode(DisplayMode::kRotate), 30);
    Timeline(fonts, text_only, "mode 2, none within 15 min: soonest bus stays",
             Data(0, {{"212", 1200, 10}, {"117", 1500, 12}}), Mode(DisplayMode::kRotate), 30);
    Timeline(fonts, text_only, "mode 2, joins at 15 min, stays at 16, leaves at 17",
             Data(0, {{"212", 965, 10}}), Mode(DisplayMode::kRotate), 30, {},
             {{10, "OK 1\nLABEL BUS\nBUS 212 990 10 RUN\n"},
              {20, "OK 1\nLABEL BUS\nBUS 212 1030 10 RUN\n"}});
    DisplaySettings keep_close = Mode(DisplayMode::kRotate);
    keep_close.rotate_min = 0;
    Timeline(fonts, text_only, "mode 2 --min 0, a SOON route stays twice as long (20 s)",
             Data(0, {{"102", 50, 1}, {"104", 300, 3}}), keep_close, 30);
    Timeline(fonts, text_only, "mode 2, buses under 3 min and SOON are skipped (3..15 min)",
             Data(0, {{"102", 50, 1}, {"104", 179, 3}, {"113", 200, 4}, {"212", 600, 8}}),
             Mode(DisplayMode::kRotate), 40);
    Timeline(fonts, text_only, "mode 2, only close buses left: original clock",
             Data(0, {{"102", 50, 1}, {"104", 170, 3}}), Mode(DisplayMode::kRotate), 5);

    // Quiet hours (default 01:00-05:00).
    Timeline(fonts, text_only, "quiet hours start at 01:00:00 (mode 2, from 00:59:55)",
             Data(0, kStop), Mode(DisplayMode::kRotate), 8, {}, {}, 59 * 60 + 55);
    Timeline(fonts, text_only, "quiet hours end at 05:00:00 (mode 1, key error, from 04:59:50)",
             ErrorSnap(BusStatus::kAuthError), Mode(DisplayMode::kAlternate), 30, {}, {},
             4 * 3600 + 59 * 60 + 50);

    printf("== feed pause decisions\n");
    struct Case { const char *what; DisplaySettings s; int sod; bool synced; };
    DisplaySettings midnight = Mode(DisplayMode::kRotate);
    midnight.quiet_start = 23 * 60 + 30;
    midnight.quiet_end = 5 * 60 + 30;
    DisplaySettings no_quiet = Mode(DisplayMode::kRotate);
    no_quiet.quiet_start = no_quiet.quiet_end = -1;
    const Case cases[] = {
        {"00:59:29 default", Mode(DisplayMode::kRotate), 59 * 60 + 29, true},
        {"00:59:30 default", Mode(DisplayMode::kRotate), 59 * 60 + 30, true},
        {"04:59:59 default", Mode(DisplayMode::kRotate), 4 * 3600 + 59 * 60 + 59, true},
        {"05:00:00 default", Mode(DisplayMode::kRotate), 5 * 3600, true},
        {"23:45 23:30-05:30", midnight, 23 * 3600 + 45 * 60, true},
        {"05:29 23:30-05:30", midnight, 5 * 3600 + 29 * 60, true},
        {"05:30 23:30-05:30", midnight, 5 * 3600 + 30 * 60, true},
        {"03:00 quiet off", no_quiet, 3 * 3600, true},
        {"14:00 mode off", Mode(DisplayMode::kOff), 14 * 3600, true},
        {"14:00 clock not synced", Mode(DisplayMode::kRotate), 14 * 3600, false},
    };
    for (const Case &c : cases) {
        printf("  %-24s %s\n", c.what, PauseName(FeedPauseFor(c.s, c.sod, c.synced)));
    }
}

// Real BusFeed thread + a fake helper: paused means no helper run at all.
int FeedTest() {
    BusFeed *feed = new BusFeed("printf 'OK 1\\nLABEL BUS\\nBUS 102 300 3 RUN\\n'", "./Makefile",
                                /*start_delay=*/0);
    auto settle = [] { struct timespec ts = { 0, 400000000L }; nanosleep(&ts, NULL); };
    int bad = 0;
    feed->SetPause(FeedPause::kQuietHours, "01:00-05:00");
    feed->Start();
    settle();
    const int paused_runs = feed->Runs();
    feed->SetPause(FeedPause::kNone);
    settle();
    const int resumed_runs = feed->Runs();
    feed->SetPause(FeedPause::kModeOff);
    settle();
    const int again_runs = feed->Runs();
    const BusSnapshot snap = feed->Snapshot();
    feed->Stop();
    printf("feed-test: paused %d run(s), after resume %d, paused again %d, %zu bus(es)\n",
           paused_runs, resumed_runs, again_runs, snap.buses.size());
    if (paused_runs != 0) bad++;
    if (resumed_runs != 1) bad++;
    if (again_runs != 1) bad++;
    if (snap.status != BusStatus::kOk || snap.buses.size() != 1) bad++;
    printf("feed-test: %s\n", bad == 0 ? "ok" : "FAILED");
    return bad == 0 ? 0 : 1;
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
    if (argc == 2 && strcmp(argv[1], "--feed-test") == 0) return FeedTest();
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
        DisplaySettings s = ParseDisplaySettings(ReadFile("./display.conf"));
        s.mode = mode;
        Timeline(fonts, false, "stdin", snap, s, secs > 0 ? secs : 20);
        return g_problems == 0 ? 0 : 1;
    }

    const bool text_only = argc == 2 && strcmp(argv[1], "--timeline") == 0;
    if (!text_only) Stills(fonts);
    Timelines(fonts, text_only);
    return g_problems == 0 ? 0 : 1;
}
