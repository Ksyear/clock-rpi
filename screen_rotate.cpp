/*
 * Raspberry Pi LED Matrix Clock - mode 2: date | HH:MM | routes in turn
 *
 * Copyright (C) 2026 Ksyear
 *
 * Same screen as mode 3, but the bus area on the right takes turns showing the
 * routes whose bus comes in `rotate_min`..`rotate_within` minutes (default
 * 3..15), in route order, `rotate_secs` each (default 10, twice as long while a
 * bus is SOON). Closer buses and SOON ones are too close to catch and are left
 * out (rotate_min 0 keeps them).
 *
 *   - per route the nearest bus, or if it is too close the one after it (a
 *     second bus in the answer, or nearest + headway for short-headway routes
 *     such as 102), tag "(2nd)"
 *   - a route joins at <= within minutes and leaves at >= within + 2
 *   - one route in range: no rotation at all
 *   - none in range: the soonest bus stays up (changes only if another one is
 *     at least 2 minutes sooner), tag "(next)"
 *   - no bus at all: "BUS --" ("BUS ..." until the first answer); the layout
 *     never changes to the original HH:MM:SS clock (only in quiet hours)
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

    // Per route, the bus worth showing: the nearest one if it is catchable,
    // else the one after it (a real second bus, or an estimate from the headway).
    struct Live {
        const BusEntry *entry;
        BusPick pick;
    };
    std::vector<Live> live;  // in route order (board.entries is sorted)
    if (board.state == BoardState::kData) {
        for (const BusEntry &e : board.entries) {
            BusPick pick;
            if (CatchableBus(e, s.rotate_min, &pick)) live.push_back(Live{ &e, pick });
        }
    }
    if (live.empty()) {
        Reset();
        return DrawSideIdle(c, f, t, board, "BUS");
    }
    auto find = [&live](const std::string &route) -> const Live * {
        for (const Live &l : live) {
            if (l.entry->route == route) return &l;
        }
        return NULL;
    };

    // Who takes part in the rotation, with hysteresis around the threshold.
    std::vector<std::string> members;
    for (const Live &l : live) {
        const bool was =
            std::find(members_.begin(), members_.end(), l.entry->route) != members_.end();
        if (l.pick.mins <= s.rotate_within || (was && l.pick.mins < s.rotate_within + 2)) {
            members.push_back(l.entry->route);
        }
    }
    members_ = members;

    const Live *shown = NULL;
    std::string suffix;
    if (members_.empty()) {
        // Nothing within the threshold: keep showing the soonest real bus, without
        // rotating (headway estimates of long-headway routes are too rough here).
        const Live *soonest = NULL;
        for (const Live &l : live) {
            if (!l.pick.estimated && (soonest == NULL || l.pick.mins < soonest->pick.mins)) {
                soonest = &l;
            }
        }
        if (soonest == NULL) {
            Reset();
            return DrawSideIdle(c, f, t, board, "BUS");
        }
        shown = find(current_);
        if (shown == NULL || shown->pick.estimated || soonest->pick.mins <= shown->pick.mins - 2) {
            shown = soonest;
        }
        if (current_ != shown->entry->route) {
            current_ = shown->entry->route;
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
            const Live *l = find(current_);
            const double dwell =
                s.rotate_secs * (l && l->pick.style == EntryStyle::kSoon ? 2 : 1);
            if (now - since_ >= dwell) {
                idx = (idx + 1) % static_cast<int>(members_.size());
                current_ = members_[idx];
                since_ += dwell;
                if (now - since_ >= s.rotate_secs) since_ = now;  // after a long pause
            }
        }
        shown = find(current_);
    }

    BusEntry view;
    view.route = shown->entry->route;
    view.mins = shown->pick.mins;
    view.style = shown->pick.style;
    const std::string text = MinutesText(view);
    const rgb_matrix::Color &color = view.style == EntryStyle::kStale ? colors::kStale
                                   : view.style == EntryStyle::kWait  ? colors::kWait
                                                                      : colors::kMinutes;
    // Blinking starts "on" every time a route comes up.
    const bool show = view.style != EntryStyle::kSoon || fmod(now - since_, 1.0) < 0.5;
    DrawSideBus(c, f, t, view.route, text, color, show);
    return "SIDE " + view.route + " " + text + (shown->pick.second ? " (2nd)" : "") + suffix;
}
