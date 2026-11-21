/*
 * Raspberry Pi LED Matrix Clock - screens for each display mode
 *
 * Copyright (C) 2026 Ksyear
 *
 *   mode 0 off        original clock only                  (screen.cpp)
 *   mode 1 alternate  clock <-> all-routes pages           (screen_alternate.cpp)
 *   mode 2 rotate     date | HH:MM | routes in turn        (screen_rotate.cpp)
 *   mode 3 fixed      date | HH:MM | one fixed route       (screen_fixed.cpp)
 *
 * Every Draw() returns a short tag describing what was drawn (without
 * blinking), used by ./preview to print timelines.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#ifndef CLOCK_SCREEN_H
#define CLOCK_SCREEN_H

#include <string>
#include <vector>

#include "bus.h"
#include "layout_common.h"
#include "settings.h"

// Mode 1: the original clock for alt_clock_secs, then pages of up to six
// routes (2 rows x 3 columns) for alt_bus_secs each, then the clock again.
class AlternateScreen {
public:
    // bus_first: start with the bus pages (used right after a mode switch).
    void Reset(double now, bool bus_first);
    std::string Draw(rgb_matrix::Canvas *c, const ClockFonts &f, const struct tm &t,
                     const BusBoard &board, const DisplaySettings &s, double now);

private:
    bool StartBus(const BusBoard &board, double now);

    bool in_bus_ = false;
    double phase_start_ = 0;
    int pages_ = 0;
    bool error_page_ = false;
    BusStatus error_ = BusStatus::kOk;
    std::vector<std::string> queue_;  // routes latched when the bus pages start
};

// Mode 2: the right-hand bus area shows, in route order, each route whose bus
// comes within rotate_within minutes, for rotate_secs (twice as long when SOON).
// A route joins at <= within minutes and leaves at >= within + 2, so a bus near
// the threshold does not flicker in and out. Buses rotate_skip minutes or less
// away (and SOON ones) are left out as too close to catch. With no route in
// range the soonest remaining bus is shown without rotating; with none, the clock.
class RotateScreen {
public:
    void Reset();
    std::string Draw(rgb_matrix::Canvas *c, const ClockFonts &f, const struct tm &t,
                     const BusBoard &board, const DisplaySettings &s, double now);

private:
    std::vector<std::string> members_;  // routes in the rotation, route order
    std::string current_;  // tracked by name, so a vanished route is skipped cleanly
    double since_ = 0;
};

// Mode 3: the right-hand bus area always shows s.route.
std::string DrawFixedScreen(rgb_matrix::Canvas *c, const ClockFonts &f, const struct tm &t,
                            const BusBoard &board, const DisplaySettings &s, double now);

// Picks the screen for the current mode and resets its state on a mode change.
class ClockScreen {
public:
    std::string Draw(rgb_matrix::Canvas *c, const ClockFonts &f, const struct tm &t,
                     const BusSnapshot &snap, const DisplaySettings &s, double now);

private:
    AlternateScreen alt_;
    RotateScreen rot_;
    DisplayMode last_ = DisplayMode::kOff;
    bool first_ = true;
};

#endif  // CLOCK_SCREEN_H
