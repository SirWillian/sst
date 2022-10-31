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

#include "event.h"
#include "intdefs.h"

DECL_EVENT(HudPaint)

struct Color {
	int r;
	int g;
	int b;
	int a;
};

const struct Color COLOR_WHITE = { 255, 255, 255, 255 };
const struct Color COLOR_RED = { 255, 0, 0, 255 };
const struct Color COLOR_GREEN = { 0, 255, 0, 255 };
const struct Color COLOR_BLUE = { 0, 0, 255, 255 };

typedef ulong HFont;

// get the handle to a font from it's name (sourcescheme.res)
HFont hud_getfont(const char *name, bool proportional);

// draw some text on the screen
void hud_drawtext(HFont font, int x, int y, struct Color color, ushort *str,
		size_t len);

#endif
