/*
 * Raspberry Pi LED Matrix Clock - display mode dispatcher
 *
 * Copyright (C) 2026 Ksyear
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include "screen.h"

std::string ClockScreen::Draw(rgb_matrix::Canvas *c, const ClockFonts &f, const struct tm &t,
                              const BusSnapshot &snap, const DisplaySettings &s, double now) {
    const BusBoard board = ComputeBusBoard(snap, now);

    if (first_ || s.mode != last_) {
        // At boot mode 1 starts with the clock; after `clockmode 1` it starts with
        // the bus pages so the switch is visible right away.
        alt_.Reset(now, /*bus_first=*/!first_);
        rot_.Reset();
        last_ = s.mode;
        first_ = false;
    }

    switch (s.mode) {
    case DisplayMode::kAlternate: return alt_.Draw(c, f, t, board, s, now);
    case DisplayMode::kRotate:    return rot_.Draw(c, f, t, board, s, now);
    case DisplayMode::kFixed:     return DrawFixedScreen(c, f, t, board, s, now);
    case DisplayMode::kOff:       break;
    }
    DrawClassicClock(c, f, t);
    return "CLOCK";
}
