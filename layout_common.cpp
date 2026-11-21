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
#include <string.h>

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

// Bus area: columns 71..95 (25 px = five 5x8 characters), right of "HH:MM".
const int kBusLeft = 71;
const int kBusWidth = 25;

// Draws `s` centered in the bus area.
void DrawBusText(rgb_matrix::Canvas *c, const Font &font, int baseline,
                 const Color &color, const std::string &s) {
    int x = kBusLeft + (kBusWidth - TextWidth(font, s) + 1) / 2;
    if (x < kBusLeft) x = kBusLeft;
    rgb_matrix::DrawText(c, font, x, baseline, color, NULL, s.c_str(), 0);
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
    // HH:MM occupies columns 30..69.
    strftime(time_buf, sizeof(time_buf), "%H:%M", &now);
    rgb_matrix::DrawText(c, *f.large, 30, 13, colors::kWhite, NULL, time_buf, 0);

    DrawBusText(c, *f.small, 7, colors::kRoute, route);
    if (show_text) DrawBusText(c, *f.small, 15, text_color, text);
}
