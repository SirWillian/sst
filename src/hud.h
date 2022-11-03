/*
 * Copyright © 2022 Matthew Wozniak <sirtomato999@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED “AS IS” AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef INC_HUD_H

#include "con_.h"
#include "event.h"
#include "intdefs.h"

DECL_EVENT(HudPaint)

struct hfont { ulong handle; };

// get the handle to a font from it's name (sourcescheme.res)
struct hfont hud_getfont(const char *name, bool proportional);

void hud_setcolour(struct con_colour colour);

void hud_drawrect(int x0, int y0, int x1, int y1, struct con_colour colour,
		bool filled);

void hud_drawline(int x0, int y0, int x1, int y1, struct con_colour colour);

// draw a string of lines with `num_points` points from arrays x and y
void hud_drawpolyline(int *x, int *y, int num_points);

void hud_drawtext(struct hfont font, int x, int y, struct con_colour c,
		ushort *str, size_t len);

void hud_getscreensize(int *width, int *height);

#endif
