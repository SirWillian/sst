/*
 * Copyright © 2022 Michael Smith <mikesmiffy128@gmail.com>
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

// enum demomsg_type {
// 	// msg types go here
// };

#define DEMO_MSG(type)
#define DEMO_STRUCT // for structs only used inside other messages
#define MSG_MEMBER(member, type, ...) type(member, __VA_ARGS__)
#define MSG_MEMBER_KEY(member, key, type, ...) MSG_MEMBER(member, type, __VA_ARGS__)

// add types as needed
// XXX: maybe fuse all number types together
#define MSG_BOOLEAN(name) bool name
#define MSG_INT(name) int name
#define MSG_ULONG(name) unsigned long name
#define MSG_STR(name, size) char name[size]
#define MSG_DYN_STR(name) char *name
#define MSG_ARRAY(name, size, type, ...) type(name, __VA_ARGS__)[size]
#define MSG_DYN_ARRAY(name, size_member, type, ...) type(*name, __VA_ARGS__)
#define MSG_MAP(name, type) type name
#define MSG_PTR(name, type, ...) type(*name, __VA_ARGS__)

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
