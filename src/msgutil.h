/*
 * Copyright © 2023 Willian Henrique <wsimanbrazil@yahoo.com.br>
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

#ifndef INC_MSGUTIL_H
#define INC_MSGUTIL_H

#include <stdlib.h>
#include <string.h>

#include "chunklets/msg.h"
#include "mem.h"

// Ensure that the given buffer has the required free space
static inline bool msg_ensurebuf(unsigned char **buf, unsigned char **head,
		int *sz, int *free, int space) {
	if (*free < space) {
		int newsz = *sz, required = newsz - *free + space;
        do { *free += newsz; newsz *= 2; } while (newsz < required);
		unsigned char *new = realloc(*buf, newsz);
		if (!new) return false;
		int off = mem_diff(*head, *buf);
		*buf = new; *head = mem_offset(new, off);
		*sz *= 2;
	}
	return true;
}

// Helper macros for writing custom messages. Read these from the bottom up

#define _MSG_PICK5413(a1, a2, a3, a4, a5, ...) a5 a4 a1 a3
#define _msg_put(buf, type, val) msg_put##type(buf, val)
#define _msg_toksz(buf, type, val, ...) \
    _MSG_PICK5413(buf +=, __VA_ARGS__, _msg_put(buf, type, val),,)
#define _msg_toksz_bool() , 1,;
#define _msg_toksz_i7() , 1,;
#define _msg_toksz_f() , 5,;
#define _msg_toksz_ssz5() , 1,;
#define _msg_toksz_bsz8() , 2,;
#define _msg_toksz_asz4() , 1,;
#define _msg_toksz_msz4() , 1,;

// Expand to "buf += msg_puttype(buf, val)" or "msg_puttype(buf, val); buf += N"
// because some msg_puttype functions don't return how many bytes were written
#define _msg_tok(buf, type, val) \
    _msg_toksz(buf, type, val, _msg_toksz_##type())

#define _msg_putnil(buf, ...) msg_putnil(buf++)
#define _msg_putssz(buf, type, val, lenval) do { \
    const int len = lenval; /* in case lenval ends up being a function call */ \
    _msg_tok(buf, type, len); \
    memcpy(buf, val, len); buf += len; \
} while(0)

#define _PICK2(x, n, ...) n
#define _msg_val(...) _PICK2(__VA_ARGS__, _msg_tok)
#define _msg_val_nil() , _msg_putnil
#define _msg_val_ssz5() , _msg_putssz
#define _msg_val_ssz8() , _msg_putssz
#define _msg_val_ssz16() , _msg_putssz
#define _msg_val_ssz() , _msg_putssz

// Pass the provided arguments to either _msg_tok or the specified macro on the
// special-cased types above
#define msg_putval(buf, type, val, ...) \
    _msg_val(_msg_val_##type())(buf, type, val __VA_OPT__(,) __VA_ARGS__)

#define msg_putkey(buf, key) do { \
    const int keylen = sizeof(key) - 1; \
    msg_putssz5(buf++, keylen); \
    memcpy(buf, key, keylen); buf += keylen; \
} while(0)

/* Write a key-value pair inside a map. The key must be a string literal */
#define msg_mkv(buf, key, type, val, ...) \
    msg_putkey(buf, key); \
    msg_putval(buf, type, val __VA_OPT__(,) __VA_ARGS__)

/* Write a 1 character long key and a value inside a map */
#define msg_mk1v(buf, key, type, val, ...) \
    msg_putssz5(buf++, 1); *buf++ = key; \
    msg_putval(buf, type, val __VA_OPT__(,) __VA_ARGS__)

/* Write the message header for a message with given type */
#define msg_header(buf, type, nkeys) \
    msg_putasz4(buf++, 2); \
    /* If we were to write type names as strings */ \
	/* msg_putssz5(p++, sizeof(#type) - 1); memcpy(p, #type, sizeof(#type) - 1); \
    p += sizeof(#type) - 1; */ \
	msg_puti7(buf++, _demomsg_##type); \
    msg_putmsz4(buf++, nkeys)

#endif
