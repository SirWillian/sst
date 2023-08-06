/*
 * Copyright © 2022 Michael Smith <mikesmiffy128@gmail.com>
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

#ifndef INC_DEMOCUSTOM_H
#define INC_DEMOCUSTOM_H

/*
 * Writes a custom demo message, automatically splitting into multiple demo
 * packets if too long. Assumes a demo is currently being recorded.
 */
void democustom_write(const void *buf, int len);
#include <demomsg.gen.h>

enum demomsg_type {
	_demomsg_hasstr,
	_demomsg_test,
};

#define EXP(x) x
#define PARENS ()
#define DEMO_MSG(type) struct type
#define DEMO_STRUCT(type) struct type // for structs only used inside other messages
// this determines MAXTYPES in cmeta
#define MSG_MEMBER(member, type, ...) \
    EXP(EXP(EXP(EXP(EXP(EXP(EXP(EXP(type PARENS (member, __VA_ARGS__)))))))))
#define MSG_MEMBER_KEY(member, key, type, ...) MSG_MEMBER(member, type, __VA_ARGS__)

#define _MSG_BOOLEAN(name) bool name
#define _MSG_INT(name) int name
#define _MSG_ULONG(name) unsigned long name
#define _MSG_FLOAT(name) float name
#define _MSG_DOUBLE(name) double name
#define _MSG_STR(name, size) char name[size]
#define _MSG_DYN_STR(name) char *name
#define _MSG_MAP(name, type) type name
#define _MSG_PTR(name, type, ...) type PARENS (*(name), __VA_ARGS__)
#define _MSG_ARRAY(name, size, type, ...) \
    type PARENS ((name)[size], __VA_ARGS__)
#define _MSG_DYN_ARRAY(name, size_member, type, ...) \
    type PARENS (*(name), __VA_ARGS__)

#define MSG_BOOLEAN() _MSG_BOOLEAN
#define MSG_INT() _MSG_INT
#define MSG_ULONG() _MSG_ULONG
#define MSG_FLOAT() _MSG_FLOAT
#define MSG_DOUBLE() _MSG_DOUBLE
#define MSG_STR() _MSG_STR
#define MSG_DYN_STR() _MSG_DYN_STR
#define MSG_MAP() _MSG_MAP
#define MSG_PTR() _MSG_PTR
#define MSG_ARRAY() _MSG_ARRAY
#define MSG_DYN_ARRAY() _MSG_DYN_ARRAY

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
