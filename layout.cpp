/*
 * Raspberry Pi LED Matrix Clock - screen layout
 *
 * Copyright (C) 2026 Ksyear
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include "layout.h"

#include <ctype.h>
#include <string.h>

#include <string>

using rgb_matrix::Color;
using rgb_matrix::Font;

namespace {

// Bus area: columns 71..95 (25 px = five 5x8 characters), right of "HH:MM".
const int kBusLeft = 71;
const int kBusWidth = 25;

const Color kRed(255, 0, 0);
const Color kGreen(0, 255, 0);
const Color kBlue(0, 0, 255);
const Color kWhite(255, 255, 255);
const Color kRoute(0, 180, 255);      // sky blue
const Color kMinutes(255, 160, 0);    // orange
const Color kStale(110, 70, 0);       // dim orange
const Color kNone(90, 90, 90);        // grey
const Color kError(160, 0, 0);        // dark red

int TextWidth(const Font &font, const std::string &s) {
    int w = 0;
    for (unsigned char ch : s) w += font.CharacterWidth(ch);
    return w;
}

// Draws `s` centered in the bus area.
void DrawBusText(rgb_matrix::Canvas *c, const Font &font, int baseline,
                 const Color &color, const std::string &s) {
    int x = kBusLeft + (kBusWidth - TextWidth(font, s) + 1) / 2;
    if (x < kBusLeft) x = kBusLeft;
    rgb_matrix::DrawText(c, font, x, baseline, color, NULL, s.c_str(), 0);
}

}  // namespace

void DrawClockScreen(rgb_matrix::Canvas *canvas, const ClockFonts &fonts,
                     const struct tm &now, const BusView &bus, bool blink_on) {
    char date_buf[12], week_buf[8], time_buf[12];
    strftime(date_buf, sizeof(date_buf), "%m-%d", &now);
    strftime(week_buf, sizeof(week_buf), "%a", &now);
    for (int i = 0; week_buf[i]; i++) {
        week_buf[i] = toupper(static_cast<unsigned char>(week_buf[i]));
    }

    // Date and weekday: Sunday red, Saturday blue, weekdays green.
    const Color *day_color = &kGreen;
    if (strcmp(week_buf, "SUN") == 0) day_color = &kRed;
    else if (strcmp(week_buf, "SAT") == 0) day_color = &kBlue;
    rgb_matrix::DrawText(canvas, *fonts.small, 3, 7, *day_color, NULL, date_buf, 0);
    rgb_matrix::DrawText(canvas, *fonts.small, 8, 15, *day_color, NULL, week_buf, 0);

    if (bus.style == BusStyle::kHidden) {
        strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &now);
        rgb_matrix::DrawText(canvas, *fonts.large, 30, 13, kWhite, NULL, time_buf, 0);
        return;
    }

    // HH:MM occupies columns 30..69.
    strftime(time_buf, sizeof(time_buf), "%H:%M", &now);
    rgb_matrix::DrawText(canvas, *fonts.large, 30, 13, kWhite, NULL, time_buf, 0);

    DrawBusText(canvas, *fonts.small, 7, kRoute, bus.route);

    const Color *color = &kMinutes;
    switch (bus.style) {
    case BusStyle::kStale: color = &kStale; break;
    case BusStyle::kNone:  color = &kNone; break;
    case BusStyle::kError: color = &kError; break;
    case BusStyle::kSoon:
        if (!blink_on) return;
        break;
    default: break;
    }
    DrawBusText(canvas, *fonts.small, 15, *color, bus.text);
}
