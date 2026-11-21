/*
 * Raspberry Pi LED Matrix Clock - mode 1: clock <-> all-routes pages
 *
 * Copyright (C) 2026 Ksyear
 *
 * Bus page layout (96x16, 5x8 font), six slots, 2 rows x 3 columns of 32 px:
 *
 *      11 26     102 23     104 23      <- baseline y=7
 *     113  5     117 23     212 35      <- baseline y=15
 *
 * route (sky blue) right-aligned at x0+20, minutes (orange, no unit) right-
 * aligned at x0+31. A blinking number means the bus is at the next stop or
 * under a minute away. Column 95 holds page dots when there are 2 pages.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <math.h>

#include "screen.h"

using rgb_matrix::Color;

namespace {

const int kSlots = 6;
const int kCols = 3;
const int kCellWidth = 32;
const int kDotColumn = 95;

void DrawCentered(rgb_matrix::Canvas *c, const rgb_matrix::Font &font, int baseline,
                  const Color &color, const std::string &s) {
    const int x = (c->width() - TextWidth(font, s) + 1) / 2;
    rgb_matrix::DrawText(c, font, x, baseline, color, NULL, s.c_str(), 0);
}

}  // namespace

void AlternateScreen::Reset(double now, bool bus_first) {
    in_bus_ = false;
    phase_start_ = bus_first ? now - 1e6 : now;
}

bool AlternateScreen::StartBus(const BusBoard &board, double now) {
    queue_.clear();
    error_page_ = false;
    pages_ = 0;
    if (board.state == BoardState::kError) {
        error_page_ = true;
        error_ = board.error;
        pages_ = 1;
    } else if (board.state == BoardState::kData) {
        for (const BusEntry &e : board.entries) {
            if (e.style != EntryStyle::kPassed) queue_.push_back(e.route);
        }
        pages_ = (static_cast<int>(queue_.size()) + kSlots - 1) / kSlots;
    }
    if (pages_ == 0) return false;  // nothing to show: keep the clock
    in_bus_ = true;
    phase_start_ = now;
    return true;
}

std::string AlternateScreen::Draw(rgb_matrix::Canvas *c, const ClockFonts &f,
                                  const struct tm &t, const BusBoard &board,
                                  const DisplaySettings &s, double now) {
    if (!in_bus_) {
        if (now - phase_start_ >= s.alt_clock_secs && !StartBus(board, now)) {
            phase_start_ = now;  // no bus to show: another clock period
        }
    }
    int page = 0;
    if (in_bus_) {
        page = static_cast<int>((now - phase_start_) / s.alt_bus_secs);
        const bool usable = error_page_ ? board.state != BoardState::kHidden
                                        : board.state == BoardState::kData;
        if (!usable || page >= pages_) {
            in_bus_ = false;
            phase_start_ = now;
        }
    }

    if (!in_bus_) {
        DrawClassicClock(c, f, t);
        return "CLOCK";
    }

    if (error_page_) {
        const bool limit = error_ == BusStatus::kLimitError;
        DrawCentered(c, *f.small, 7, colors::kRoute, "BUS INFO");
        DrawCentered(c, *f.small, 15, colors::kError, limit ? "DAILY LIMIT" : "KEY ERROR");
        return limit ? "BUS ERR LIMIT" : "BUS ERR KEY";
    }

    // Blinking restarts with every page, so a page never opens with a dark slot.
    const double page_start = phase_start_ + page * s.alt_bus_secs;
    const bool blink_on = fmod(now - page_start, 1.0) < 0.5;

    std::string tag = "BUS " + std::to_string(page + 1) + "/" + std::to_string(pages_);
    for (int i = 0; i < kSlots; i++) {
        const size_t idx = static_cast<size_t>(page * kSlots + i);
        if (idx >= queue_.size()) break;
        const BusEntry *e = FindEntry(board, queue_[idx]);
        // A bus that passed while its page is up leaves its slot empty: nothing moves.
        if (e == NULL || e->style == EntryStyle::kPassed) continue;

        const int x0 = kCellWidth * (i % kCols);
        const int y = i < kCols ? 7 : 15;
        rgb_matrix::DrawText(c, *f.small, x0 + 20 - TextWidth(*f.small, e->route), y,
                             colors::kRoute, NULL, e->route.c_str(), 0);

        const std::string mins = std::to_string(e->mins);
        const Color &color = e->style == EntryStyle::kStale ? colors::kStale : colors::kMinutes;
        if (e->style != EntryStyle::kSoon || blink_on) {
            rgb_matrix::DrawText(c, *f.small, x0 + 31 - TextWidth(*f.small, mins), y,
                                 color, NULL, mins.c_str(), 0);
        }
        tag += " " + e->route + ":" + MinutesText(*e);
    }

    if (pages_ > 1) {
        for (int p = 0; p < pages_; p++) {
            const Color &dot = p == page ? colors::kRoute : colors::kNone;
            const int y = 4 + 6 * p;
            c->SetPixel(kDotColumn, y, dot.r, dot.g, dot.b);
            c->SetPixel(kDotColumn, y + 1, dot.r, dot.g, dot.b);
        }
    }
    return tag;
}
