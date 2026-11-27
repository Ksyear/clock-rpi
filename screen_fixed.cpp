/*
 * Raspberry Pi LED Matrix Clock - mode 3: date | HH:MM | one fixed route
 *
 * Copyright (C) 2026 Ksyear
 *
 *   MM-DD |          | 102     <- route (sky blue), `clockmode 3 --route 102`
 *    DAY  |  HH:MM   | 3M      <- minutes (orange), SOON blinks, "--" passed
 *
 * Without data for that route the bus area shows "102 --" ("102 ..." until the
 * first answer), never the original clock. Right after the bus has passed, the
 * next one (real or estimated from the headway, lavender) is shown instead of "--".
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include "screen.h"

std::string DrawFixedScreen(rgb_matrix::Canvas *c, const ClockFonts &f, const struct tm &t,
                            const BusBoard &board, const DisplaySettings &s, double now) {
    if (board.state == BoardState::kError) {
        DrawSideBus(c, f, t, s.route, "ERR", colors::kError, true);
        return "SIDE " + s.route + " ERR";
    }
    const BusEntry *e = board.state == BoardState::kData ? FindEntry(board, s.route) : NULL;
    if (e == NULL) return DrawSideIdle(c, f, t, board, s.route);

    if (e->style == EntryStyle::kPassed && e->has_next) {
        BusEntry next;
        next.route = e->route;
        next.mins = e->next_mins;
        next.style = e->next_style;
        const std::string text = MinutesText(next);
        const rgb_matrix::Color &color = next.style == EntryStyle::kWait  ? colors::kWait
                                       : next.style == EntryStyle::kStale ? colors::kStale
                                                                          : colors::kMinutes;
        const bool show = next.style != EntryStyle::kSoon ||
                          (static_cast<long long>(now * 2) % 2) == 0;
        DrawSideBus(c, f, t, e->route, text, color, show);
        return "SIDE " + e->route + " " + text + " (2nd)";
    }

    const std::string text = MinutesText(*e);
    const rgb_matrix::Color *color = &colors::kMinutes;
    if (e->style == EntryStyle::kStale) color = &colors::kStale;
    else if (e->style == EntryStyle::kPassed) color = &colors::kNone;
    else if (e->style == EntryStyle::kWait) color = &colors::kWait;
    const bool show = e->style != EntryStyle::kSoon || (static_cast<long long>(now * 2) % 2) == 0;
    DrawSideBus(c, f, t, e->route, text, *color, show);
    return "SIDE " + e->route + " " + text;
}
