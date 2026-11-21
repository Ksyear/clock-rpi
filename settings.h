/*
 * Raspberry Pi LED Matrix Clock - display mode settings (./display.conf)
 *
 * Copyright (C) 2026 Ksyear
 *
 * The file is written by the `clockmode` command and re-read by the clock about
 * once a second, so a mode change shows up on the LEDs within ~1.2 s.
 * Defaults and limits here must match `clockmode` (tests/test_clockmode.py checks).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#ifndef CLOCK_SETTINGS_H
#define CLOCK_SETTINGS_H

#include <sys/types.h>
#include <time.h>

#include <string>

enum class DisplayMode {
    kOff,        // 0: original clock only, no API calls
    kAlternate,  // 1: clock <-> all-routes screen
    kRotate,     // 2: date | HH:MM | routes within N minutes in turn
    kFixed,      // 3: date | HH:MM | one fixed route
};

struct DisplaySettings {
    DisplayMode mode = DisplayMode::kRotate;
    std::string route = "102";  // mode 3
    int alt_clock_secs = 15;    // mode 1: clock screen          (5..300)
    int alt_bus_secs = 8;       // mode 1: each bus page         (2..30)
    int rotate_secs = 6;        // mode 2: each route            (2..60)
    int rotate_within = 15;     // mode 2: routes within N min   (5..99)
    int quiet_start = 60;       // minutes of day, -1 = no quiet hours (01:00)
    int quiet_end = 300;        // (05:00)
};

const char *ModeName(DisplayMode m);

// Missing or invalid values fall back to the defaults; numbers are clamped.
// Only ASCII is interpreted, lines end at '\n' only (same rules as clockmode).
DisplaySettings ParseDisplaySettings(const std::string &text);

// "mode=rotate route=102 alt_clock=15 alt_bus=8 rotate=6 within=15 quiet=01:00-05:00"
// (same as `clockmode --dump`).
std::string DumpDisplaySettings(const DisplaySettings &s);

// True during the quiet window [start, end) (the window may cross midnight).
bool InQuietHours(const DisplaySettings &s, int secs_of_day);

// "01:00-05:00" or "off".
std::string QuietText(const DisplaySettings &s);

class SettingsFile {
public:
    explicit SettingsFile(const std::string &path);
    // Checks the file at most once a second; returns true when the settings changed.
    bool Poll(double now);
    const DisplaySettings &Get() const { return settings_; }

private:
    const std::string path_;
    double last_check_ = -1e9;
    bool loaded_ = false;
    bool exists_ = false;
    ino_t ino_ = 0;
    off_t size_ = 0;
    time_t mtime_ = 0;
    DisplaySettings settings_;
};

#endif  // CLOCK_SETTINGS_H
