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
extern const rgb_matrix::Color kWait;     // lavender: bus still at its terminal (timetable)
}  // namespace colors

int TextWidth(const rgb_matrix::Font &font, const std::string &s);

// Date "MM-DD" 3 dots from the left edge and the weekday centered under it
// (Sunday red, Saturday blue, weekdays green).
void DrawDate(rgb_matrix::Canvas *c, const ClockFonts &f, const struct tm &now);

// The original screen: date | HH:MM:SS.
void DrawClassicClock(rgb_matrix::Canvas *c, const ClockFonts &f, const struct tm &now);

// date | HH:MM | [bus icon] route over text, laid out from the font sizes:
// 3 dots at both edges, the same gap on both sides of HH:MM (big font), so with
// the stock fonts 3 | date 3..25 | 3 | HH:MM 29..67 | 3 | bus 71..92 | 3.
// `show_text` false leaves the bottom line dark.
void DrawSideBus(rgb_matrix::Canvas *c, const ClockFonts &f, const struct tm &now,
                 const std::string &route, const std::string &text,
                 const rgb_matrix::Color &text_color, bool show_text);

#endif  // CLOCK_LAYOUT_COMMON_H
