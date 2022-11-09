/*
 * Copyright © 2022 Michael Smith <mikesmiffy128@gmail.com>
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


#include <string.h>

#include "intdefs.h"

void hexparse(uchar out[static 4], const char *s) {
	const char *p = s;
	for (uchar *q = out; q - out < 4 && *p; ++q) {
		if (*p >= '0' && *p <= '9') {
			*q = *p++ - '0' << 4;
		}
		else if ((*p | 32) >= 'a' && (*p | 32) <= 'f') {
			*q = 10 + (*p++ | 32) - 'a' << 4;
		}
		else {
			// screw it, just fall back on white, I guess.
			// note: this also handles *p == '\0' so we don't overrun the string
			memset(out, 255, 4); // write 4 rather than 3, prolly faster?
			return;
		}
		// repetitive unrolled nonsense
		if (*p >= '0' && *p <= '9') {
			*q |= *p++ - '0';
		}
		else if ((*p | 32) >= 'a' && (*p | 32) <= 'f') {
			*q |= 10 + (*p++ | 32) - 'a';
		}
		else {
			memset(out, 255, 4);
			return;
		}
	}
}

