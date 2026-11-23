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

// Side-bus screen, left-right symmetric around the middle of the 96 px panel.
// Centers are given doubled so half pixels can be expressed (95 = column 47.5).
const int kScreenCenter2 = 95;               // HH:MM ink centered on column 47.5
const int kDateCenter2 = 29;                 // date/weekday ink center (column 14.5,
                                             // where DrawDate has always put it)
const int kBusCenter2 = 2 * kScreenCenter2 - kDateCenter2;  // mirror: column 80.5

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

// x that puts the ink of `s` centered on column center2 / 2 (ties go left).
int CenteredX(const Font &font, const std::string &s, int center2) {
    InkMeter m;
    rgb_matrix::DrawText(&m, font, 0, font.baseline(), colors::kWhite, NULL, s.c_str(), 0);
    if (m.hi < m.lo) return center2 / 2;
    const int twice = center2 - m.lo - m.hi;
    return twice >= 0 ? twice / 2 : -((1 - twice) / 2);
}

void DrawCenteredText(rgb_matrix::Canvas *c, const Font &font, int center2, int baseline,
                      const Color &color, const std::string &s) {
    rgb_matrix::DrawText(c, font, CenteredX(font, s, center2), baseline, color, NULL,
                         s.c_str(), 0);
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
const int kMaxRouteLineWidth = 25;  // wider (4-character routes) -> route only

// Top line of the bus block: [bus] route, centered as one unit, so it is as
// wide as the date "MM-DD" on the other side.
void DrawRouteLine(rgb_matrix::Canvas *c, const Font &font, int center2, int baseline,
                   const Color &color, const std::string &route) {
    InkMeter m;
    rgb_matrix::DrawText(&m, font, 0, font.baseline(), colors::kWhite, NULL, route.c_str(), 0);
    const int text_width = m.hi >= m.lo ? m.hi - m.lo + 1 : 0;
    const int width = kBusIconWidth + kBusIconGap + text_width;
    if (text_width == 0 || width > kMaxRouteLineWidth) {
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
    rgb_matrix::DrawText(c, font, left + kBusIconWidth + kBusIconGap - m.lo, baseline, color,
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
    rgb_matrix::DrawText(c, *f.small, 3, 7, *day_color, NULL, date_buf, 0);
    rgb_matrix::DrawText(c, *f.small, 8, 15, *day_color, NULL, week_buf, 0);
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
    // HH:MM in the exact middle; placed by the ink of "00:00" so it does not
    // shift when the digits change.
    strftime(time_buf, sizeof(time_buf), "%H:%M", &now);
    rgb_matrix::DrawText(c, *f.large, CenteredX(*f.large, "00:00", kScreenCenter2), 13,
                         colors::kWhite, NULL, time_buf, 0);

    // [bus] route over minutes, centered at the mirror image of the date block.
    DrawRouteLine(c, *f.small, kBusCenter2, 7, colors::kRoute, route);
    if (show_text) DrawCenteredText(c, *f.small, kBusCenter2, 15, text_color, text);
}
