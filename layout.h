/*
 * Raspberry Pi LED Matrix Clock - screen layout
 *
 * Copyright (C) 2026 Ksyear
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#ifndef CLOCK_LAYOUT_H
#define CLOCK_LAYOUT_H

#include <time.h>

#include "bus.h"
#include "canvas.h"
#include "graphics.h"

struct ClockFonts {
    const rgb_matrix::Font *small;  // 5x8.bdf: date, weekday, bus
    const rgb_matrix::Font *large;  // 8x13B.bdf: time
};

// Draws one frame on a 96x16 canvas (call canvas->Clear() first).
//   bus.style == kHidden : MM-DD / DAY | HH:MM:SS          (the original screen)
//   otherwise            : MM-DD / DAY | HH:MM | route / minutes
// `blink_on` toggles the blinking "SOON" text.
void DrawClockScreen(rgb_matrix::Canvas *canvas, const ClockFonts &fonts,
                     const struct tm &now, const BusView &bus, bool blink_on);

#endif  // CLOCK_LAYOUT_H
