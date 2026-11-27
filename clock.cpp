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
 * - No API requests during quiet hours or before the clock is synchronized.
 * - Next bus of short-headway routes from the route headway (노선정보조회).
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
#include <grp.h>
#include <unistd.h>
#include <sys/resource.h>
#ifdef __linux__
#include <sys/timex.h>
#endif

using namespace rgb_matrix;

volatile bool interrupt_received = false;

static void InterruptHandler(int /*signo*/) {
        interrupt_received = true;
}

// Same test as `timedatectl` ("System clock synchronized"): read-only adjtimex().
static bool KernelClockSynced() {
#ifdef __linux__
    struct timex tx = {};
    return adjtimex(&tx) != -1 && tx.maxerror < 16000000;
#else
    return true;
#endif
}

// The Pi has no RTC: after a power cut it boots with the last saved time until
// NTP answers, so quiet hours can't be trusted yet. Wait for the sync, but give
// up after 5 minutes (e.g. NTP blocked) so bus data is not lost forever.
class ClockSyncWatch {
public:
    explicit ClockSyncWatch(double start) : start_(start) {}
    bool Ok(double now) {
        if (ok_) return true;
        if (now - last_ < 1.0) return false;
        last_ = now;
        if (KernelClockSynced()) {
            ok_ = true;
            if (now - start_ > 2) std::cerr << "[clock] system clock synchronized" << std::endl;
        } else if (now - start_ > 300) {
            ok_ = true;
            std::cerr << "[clock] still not synchronized after 300 s, using the system time"
                      << std::endl;
        }
        return ok_;
    }

private:
    const double start_;
    double last_ = -1e9;
    bool ok_ = false;
};

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
    // Hardening before the library drops root to `daemon`: it changes uid/gid but
    // keeps root's supplementary groups, so clear them; and never write core dumps
    // (a crashing helper must not leave the service key on disk).
    struct rlimit no_core = { 0, 0 };
    setrlimit(RLIMIT_CORE, &no_core);
    if (geteuid() == 0) setgroups(0, NULL);

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
    // network never blocks this loop. Without ./bus.conf there is no bus data
    // (modes 2/3 show "--"). Relative paths are used on purpose: after CreateFromOptions()
    // the process runs as `daemon`, which can only reach the working directory
    // (it cannot pass through /home/clock). The script goes in on stdin because
    // python3 opens a script given by name through its absolute path.
    // (The feed is intentionally never deleted; its thread may outlive main().)
    // The first request waits 20 s, so a crash/restart loop can't become a request loop.
    // Every few hours the --routes variant also fetches the headways (next-bus estimate).
    BusFeed *bus = new BusFeed("exec timeout -k 5 20 python3 -I - < ./busfetch.py", "./bus.conf",
                               /*start_delay=*/20,
                               "exec timeout -k 5 20 python3 -I - --routes < ./busfetch.py");
    ClockSyncWatch sync(MonotonicSeconds());
    FeedPause pause = FeedPause::kNone;
    ClockScreen screen;

    FrameCanvas *canvas = matrix->CreateFrameCanvas();

    struct timespec sleep_time;
    sleep_time.tv_sec = 0;
    sleep_time.tv_nsec = 100000000L;

    bool started = false;
    while (!interrupt_received) {
        time_t now = time(0);
        struct tm ltm;
        localtime_r(&now, &ltm);

        const double mono = MonotonicSeconds();
        settings.Poll(mono);

        // Pause polling for mode off, quiet hours (from 30 s before they start)
        // and an unsynchronized clock; resume within one frame afterwards.
        const int secs_of_day = ltm.tm_hour * 3600 + ltm.tm_min * 60 + ltm.tm_sec;
        const FeedPause want = FeedPauseFor(settings.Get(), secs_of_day, sync.Ok(mono));
        if (want != pause || !started) {
            bus->SetPause(want, want == FeedPause::kQuietHours ? QuietText(settings.Get()) : "");
            pause = want;
        }
        if (!started) {
            bus->Start();
            started = true;
        }

        canvas->Clear();
        screen.Draw(canvas, fonts, ltm, bus->Snapshot(), settings.Get(), mono);
        canvas = matrix->SwapOnVSync(canvas);

        nanosleep(&sleep_time, NULL);
    }

    bus->Stop();
    matrix->Clear();
    delete matrix;
    return 0;
}
