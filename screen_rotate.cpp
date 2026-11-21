/*
 * Raspberry Pi LED Matrix Clock - mode 2: date | HH:MM | routes in turn
 *
 * Copyright (C) 2026 Ksyear
 *
 * Same screen as mode 3, but the bus area on the right shows every route at the
 * stop in turn (route order: 11, 102, 104, 113, ...), rotate_secs each.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <math.h>

#include "screen.h"

void RotateScreen::Reset() {
    current_.clear();
    since_ = 0;
}

std::string RotateScreen::Draw(rgb_matrix::Canvas *c, const ClockFonts &f, const struct tm &t,
                               const BusBoard &board, const DisplaySettings &s, double now) {
    if (board.state == BoardState::kError) {
        current_.clear();
        DrawSideBus(c, f, t, "BUS", "ERR", colors::kError, true);
        return "SIDE BUS ERR";
    }

    std::vector<const BusEntry *> live;
    if (board.state == BoardState::kData) {
        for (const BusEntry &e : board.entries) {
            if (e.style != EntryStyle::kPassed) live.push_back(&e);
        }
    }
    if (live.empty()) {
        current_.clear();
        DrawClassicClock(c, f, t);
        return "CLOCK";
    }

    int idx = -1;
    for (size_t i = 0; i < live.size(); i++) {
        if (live[i]->route == current_) idx = static_cast<int>(i);
    }
    if (idx < 0) {
        // First frame, or the route on screen has just passed: take the next one.
        idx = 0;
        if (!current_.empty()) {
            for (size_t i = 0; i < live.size(); i++) {
                if (RouteLess(current_, live[i]->route)) {
                    idx = static_cast<int>(i);
                    break;
                }
            }
        }
        current_ = live[idx]->route;
        since_ = now;
    } else if (live.size() > 1 && now - since_ >= s.rotate_secs) {
        idx = (idx + 1) % static_cast<int>(live.size());
        current_ = live[idx]->route;
        since_ += s.rotate_secs;
        if (now - since_ >= s.rotate_secs) since_ = now;  // after a long pause
    }

    const BusEntry &e = *live[idx];
    const std::string text = MinutesText(e);
    const rgb_matrix::Color &color =
        e.style == EntryStyle::kStale ? colors::kStale : colors::kMinutes;
    // Blinking starts "on" every time a route comes up.
    const bool show = e.style != EntryStyle::kSoon || fmod(now - since_, 1.0) < 0.5;
    DrawSideBus(c, f, t, e.route, text, color, show);
    return "SIDE " + e.route + " " + text;
}
