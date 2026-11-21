/*
 * Raspberry Pi LED Matrix Clock - mode 2: date | HH:MM | routes in turn
 *
 * Copyright (C) 2026 Ksyear
 *
 * Same screen as mode 3, but the bus area on the right takes turns showing the
 * routes whose bus comes within `rotate_within` minutes (default 15), in route
 * order, `rotate_secs` each (default 6, twice as long while a bus is SOON).
 * Buses `rotate_skip` minutes or less away (default 3) and SOON buses are left
 * out: they are too close to catch (rotate_skip 0 keeps them).
 *
 *   - a route joins at <= within minutes and leaves at >= within + 2
 *   - one route in range: no rotation at all
 *   - none in range: the soonest bus stays up (changes only if another one is
 *     at least 2 minutes sooner), tag "(next)"
 *   - no bus at all: the original HH:MM:SS clock
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <math.h>

#include <algorithm>

#include "screen.h"

void RotateScreen::Reset() {
    members_.clear();
    current_.clear();
    since_ = 0;
}

std::string RotateScreen::Draw(rgb_matrix::Canvas *c, const ClockFonts &f, const struct tm &t,
                               const BusBoard &board, const DisplaySettings &s, double now) {
    if (board.state == BoardState::kError) {
        Reset();
        DrawSideBus(c, f, t, "BUS", "ERR", colors::kError, true);
        return "SIDE BUS ERR";
    }

    std::vector<const BusEntry *> live;  // in route order (board.entries is sorted)
    if (board.state == BoardState::kData) {
        for (const BusEntry &e : board.entries) {
            if (e.style == EntryStyle::kPassed) continue;
            if (s.rotate_skip > 0 && (e.style == EntryStyle::kSoon || e.mins <= s.rotate_skip)) {
                continue;  // too close to catch
            }
            live.push_back(&e);
        }
    }
    if (live.empty()) {
        Reset();
        DrawClassicClock(c, f, t);
        return "CLOCK";
    }

    // Who takes part in the rotation, with hysteresis around the threshold.
    std::vector<std::string> members;
    for (const BusEntry *e : live) {
        const bool was = std::find(members_.begin(), members_.end(), e->route) != members_.end();
        if (e->mins <= s.rotate_within || (was && e->mins < s.rotate_within + 2)) {
            members.push_back(e->route);
        }
    }
    members_ = members;

    const BusEntry *shown = NULL;
    std::string suffix;
    if (members_.empty()) {
        // Nothing within the threshold: keep showing the soonest bus, without rotating.
        const BusEntry *soonest = live[0];
        for (const BusEntry *e : live) {
            if (e->mins < soonest->mins) soonest = e;
        }
        shown = FindEntry(board, current_);
        if (shown == NULL || shown->style == EntryStyle::kPassed ||
            soonest->mins <= shown->mins - 2) {
            shown = soonest;
        }
        if (current_ != shown->route) {
            current_ = shown->route;
            since_ = now;
        }
        suffix = " (next)";
    } else {
        int idx = -1;
        for (size_t i = 0; i < members_.size(); i++) {
            if (members_[i] == current_) idx = static_cast<int>(i);
        }
        if (idx < 0) {
            // First frame, or the route on screen left the rotation: take the next one.
            idx = 0;
            if (!current_.empty()) {
                for (size_t i = 0; i < members_.size(); i++) {
                    if (RouteLess(current_, members_[i])) {
                        idx = static_cast<int>(i);
                        break;
                    }
                }
            }
            current_ = members_[idx];
            since_ = now;
        } else if (members_.size() > 1) {
            const BusEntry *e = FindEntry(board, current_);
            const double dwell = s.rotate_secs * (e && e->style == EntryStyle::kSoon ? 2 : 1);
            if (now - since_ >= dwell) {
                idx = (idx + 1) % static_cast<int>(members_.size());
                current_ = members_[idx];
                since_ += dwell;
                if (now - since_ >= s.rotate_secs) since_ = now;  // after a long pause
            }
        }
        shown = FindEntry(board, current_);
    }

    const std::string text = MinutesText(*shown);
    const rgb_matrix::Color &color = shown->style == EntryStyle::kStale ? colors::kStale
                                   : shown->style == EntryStyle::kWait  ? colors::kWait
                                                                        : colors::kMinutes;
    // Blinking starts "on" every time a route comes up.
    const bool show = shown->style != EntryStyle::kSoon || fmod(now - since_, 1.0) < 0.5;
    DrawSideBus(c, f, t, shown->route, text, color, show);
    return "SIDE " + shown->route + " " + text + suffix;
}
