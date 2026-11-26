/*
 * Raspberry Pi LED Matrix Clock - drawing shared by every screen
 *
 * Copyright (C) 2026 Ksyear
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include "layout_common.h"

#include <ctype.h>
#include <limits.h>
#include <string.h>

#include <algorithm>

using rgb_matrix::Color;
using rgb_matrix::Font;

namespace colors {
const Color kRed(255, 0, 0);
const Color kGreen(0, 255, 0);
const Color kBlue(0, 0, 255);
const Color kWhite(255, 255, 255);
const Color kRoute(0, 180, 255);
const Color kMinutes(255, 160, 0);
const Color kStale(110, 70, 0);
const Color kNone(90, 90, 90);
const Color kError(160, 0, 0);
const Color kWait(170, 120, 255);
}  // namespace colors

namespace {

// A canvas that only records which columns get ink.
class InkMeter : public rgb_matrix::Canvas {
public:
    int width() const override { return 1024; }
    int height() const override { return 64; }
    void SetPixel(int x, int /*y*/, uint8_t, uint8_t, uint8_t) override {
        lo = std::min(lo, x);
        hi = std::max(hi, x);
    }
    void Clear() override {}
    void Fill(uint8_t, uint8_t, uint8_t) override {}
    int lo = INT_MAX;
    int hi = INT_MIN;
};

// Ink columns of `s` drawn at x = 0; false if nothing is lit.
bool InkSpan(const Font &font, const std::string &s, int *lo, int *hi) {
    InkMeter m;
    rgb_matrix::DrawText(&m, font, 0, font.baseline(), colors::kWhite, NULL, s.c_str(), 0);
    if (m.hi < m.lo) return false;
    *lo = m.lo;
    *hi = m.hi;
    return true;
}

int InkWidth(const Font &font, const std::string &s) {
    int lo, hi;
    return InkSpan(font, s, &lo, &hi) ? hi - lo + 1 : 0;
}

// x that centers the ink of `s` on column center2 / 2; a half-pixel tie goes
// left, or right with `tie_right`.
int CenteredX(const Font &font, const std::string &s, int center2, bool tie_right = false) {
    int lo, hi;
    if (!InkSpan(font, s, &lo, &hi)) return center2 / 2;
    const int twice = center2 - lo - hi + (tie_right ? 1 : 0);
    return twice >= 0 ? twice / 2 : -((1 - twice) / 2);
}

// A small bus (side view), as tall as the 5x8 digits (rows 1..6 of the top line).
const char *const kBusIcon[] = {
    ".#####.",
    "#.#.#.#",
    "#######",
    "#######",
    "#######",
    ".#...#.",
};
const int kBusIconWidth = 7;
const int kBusIconGap = 2;
const int kBusIconTop = 1;

// The side-bus screen, computed from the fonts:
//
//   | margin | date block | gap | HH:MM | gap | bus block | margin |
//
// margin = 3 dots on both sides; the date block is as wide as the ink of
// "00-00", HH:MM as the ink of "00:00" (big font, fixed so the digits never
// shift), the bus block as "[bus]000"; both gaps are equal. With the stock
// fonts: 3 | 3..25 | 3 | 29..67 | 3 | 71..92 | 3.
const int kMargin = 3;
const int kPanelWidth = 96;

struct SideLayout {
    int date_x;          // DrawText x for "MM-DD"
    int date_lo, date_hi;
    int clock_x;         // DrawText x for "HH:MM"
    int bus_lo, bus_hi;  // bus block ink columns
    int gap;
};

SideLayout ComputeLayout(const ClockFonts &f) {
    SideLayout l;
    int d_lo = 0, d_hi = 0, c_lo = 0, c_hi = 0;
    InkSpan(*f.small, "00-00", &d_lo, &d_hi);
    InkSpan(*f.large, "00:00", &c_lo, &c_hi);
    const int date_w = d_hi - d_lo + 1;
    const int clock_w = c_hi - c_lo + 1;
    const int bus_w = kBusIconWidth + kBusIconGap + InkWidth(*f.small, "000");
    const int free = kPanelWidth - 2 * kMargin - date_w - clock_w - bus_w;
    l.gap = std::max(1, free / 2);  // an odd leftover pixel widens the bus block
    l.date_lo = kMargin;
    l.date_hi = kMargin + date_w - 1;
    l.date_x = l.date_lo - d_lo;
    const int clock_lo = l.date_hi + 1 + l.gap;
    l.clock_x = clock_lo - c_lo;
    l.bus_lo = clock_lo + clock_w + l.gap;
    l.bus_hi = kPanelWidth - 1 - kMargin;
    return l;
}

const SideLayout &Layout(const ClockFonts &f) {
    static const Font *small = NULL, *large = NULL;
    static SideLayout cached;
    if (small != f.small || large != f.large) {
        cached = ComputeLayout(f);
        small = f.small;
        large = f.large;
    }
    return cached;
}

void DrawCenteredText(rgb_matrix::Canvas *c, const Font &font, int center2, int baseline,
                      const Color &color, const std::string &s) {
    rgb_matrix::DrawText(c, font, CenteredX(font, s, center2), baseline, color, NULL,
                         s.c_str(), 0);
}

// Top line of the bus block: [bus] route, centered in the block as one unit
// ("[bus]102" fills it exactly). Too wide (4-character routes): route only.
void DrawRouteLine(rgb_matrix::Canvas *c, const Font &font, const SideLayout &l, int baseline,
                   const Color &color, const std::string &route) {
    const int center2 = l.bus_lo + l.bus_hi;
    int lo, hi;
    if (!InkSpan(font, route, &lo, &hi)) return;
    const int width = kBusIconWidth + kBusIconGap + (hi - lo + 1);
    if (width > l.bus_hi - l.bus_lo + 1) {
        DrawCenteredText(c, font, center2, baseline, color, route);
        return;
    }
    const int twice = center2 - width + 1;        // left ink column, doubled
    const int left = twice >= 0 ? twice / 2 : -((1 - twice) / 2);
    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < kBusIconWidth; col++) {
            if (kBusIcon[row][col] == '#') {
                c->SetPixel(left + col, kBusIconTop + row, color.r, color.g, color.b);
            }
        }
    }
    rgb_matrix::DrawText(c, font, left + kBusIconWidth + kBusIconGap - lo, baseline, color,
                         NULL, route.c_str(), 0);
}

}  // namespace

int TextWidth(const Font &font, const std::string &s) {
    int w = 0;
    for (unsigned char ch : s) w += font.CharacterWidth(ch);
    return w;
}

void DrawDate(rgb_matrix::Canvas *c, const ClockFonts &f, const struct tm &now) {
    char date_buf[12], week_buf[8];
    strftime(date_buf, sizeof(date_buf), "%m-%d", &now);
    strftime(week_buf, sizeof(week_buf), "%a", &now);
    for (int i = 0; week_buf[i]; i++) {
        week_buf[i] = toupper(static_cast<unsigned char>(week_buf[i]));
    }

    const Color *day_color = &colors::kGreen;
    if (strcmp(week_buf, "SUN") == 0) day_color = &colors::kRed;
    else if (strcmp(week_buf, "SAT") == 0) day_color = &colors::kBlue;
    // Date block: 3 dots from the left edge, weekday centered under the date
    // (a half-pixel tie goes right; the bus block mirrors it to the left).
    const SideLayout &l = Layout(f);
    rgb_matrix::DrawText(c, *f.small, l.date_x, 7, *day_color, NULL, date_buf, 0);
    rgb_matrix::DrawText(c, *f.small, CenteredX(*f.small, week_buf, l.date_lo + l.date_hi, true),
                         15, *day_color, NULL, week_buf, 0);
}

void DrawClassicClock(rgb_matrix::Canvas *c, const ClockFonts &f, const struct tm &now) {
    char time_buf[12];
    DrawDate(c, f, now);
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &now);
    rgb_matrix::DrawText(c, *f.large, 30, 13, colors::kWhite, NULL, time_buf, 0);
}

void DrawSideBus(rgb_matrix::Canvas *c, const ClockFonts &f, const struct tm &now,
                 const std::string &route, const std::string &text,
                 const Color &text_color, bool show_text) {
    char time_buf[12];
    DrawDate(c, f, now);
    // HH:MM between equal gaps, placed by the ink of "00:00" so it does not shift
    // when the digits change.
    const SideLayout &l = Layout(f);
    strftime(time_buf, sizeof(time_buf), "%H:%M", &now);
    rgb_matrix::DrawText(c, *f.large, l.clock_x, 13, colors::kWhite, NULL, time_buf, 0);

    // [bus] route over minutes, centered in the bus block (3 dots from the right edge).
    DrawRouteLine(c, *f.small, l, 7, colors::kRoute, route);
    if (show_text) DrawCenteredText(c, *f.small, l.bus_lo + l.bus_hi, 15, text_color, text);
}
