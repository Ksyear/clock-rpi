/*
 * Raspberry Pi LED Matrix Clock - drawing shared by every screen
 *
 * Copyright (C) 2026 Ksyear
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#ifndef CLOCK_LAYOUT_COMMON_H
#define CLOCK_LAYOUT_COMMON_H

#include <time.h>

#include <string>

#include "canvas.h"
#include "graphics.h"

struct ClockFonts {
    const rgb_matrix::Font *small;  // 5x8.bdf: date, weekday, bus
    const rgb_matrix::Font *large;  // 8x13B.bdf: time
};

namespace colors {
extern const rgb_matrix::Color kRed;
extern const rgb_matrix::Color kGreen;
extern const rgb_matrix::Color kBlue;
extern const rgb_matrix::Color kWhite;
extern const rgb_matrix::Color kRoute;    // sky blue: route numbers
extern const rgb_matrix::Color kMinutes;  // orange: minutes left
extern const rgb_matrix::Color kStale;    // dim orange: old data
extern const rgb_matrix::Color kNone;     // grey: "--", inactive page dot
extern const rgb_matrix::Color kError;    // dark red: errors
}  // namespace colors

int TextWidth(const rgb_matrix::Font &font, const std::string &s);

// Date "MM-DD" and weekday (Sunday red, Saturday blue, weekdays green), columns 3..27.
void DrawDate(rgb_matrix::Canvas *c, const ClockFonts &f, const struct tm &now);

// The original screen: date | HH:MM:SS.
void DrawClassicClock(rgb_matrix::Canvas *c, const ClockFonts &f, const struct tm &now);

// date | HH:MM | route over text, in the bus area (columns 71..95).
// `show_text` false leaves the bottom line dark (blinking).
void DrawSideBus(rgb_matrix::Canvas *c, const ClockFonts &f, const struct tm &now,
                 const std::string &route, const std::string &text,
                 const rgb_matrix::Color &text_color, bool show_text);

#endif  // CLOCK_LAYOUT_COMMON_H
