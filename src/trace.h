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

#ifndef INC_TRACE_H
#define INC_TRACE_H

#include "engineapi.h"
#include "intdefs.h"

// trace_t / CGameTrace
struct trace {
	// CBaseTrace
	struct vec3f startpos, endpos;
	struct plane {
		struct vec3f normal;
		float dist;
		u8 type, signbits, pad[2];
	} plane; // surface normal at impact
	float frac;
	int contents;
	ushort dispflags;
	bool allsolid, startsolid;
	// CGameTrace
	float fracleftsolid;
	struct surface {
		const char *name;
		short surfprops;
		ushort flags;
	} surf;
	int hitgroup;
	short physicsbone;
	ushort worldsurfidx; // not in every branch, but doesn't break ABI
	void *ent; // CBaseEntity (C_BaseEntity in client.dll)
	int hitbox;
};

/*
 * Traces a line from start to end, choosing what to collide against based on
 * the provided mask and optionally filtering out collisions using a filter
 * object. tr must point to a valid object and will be filled with the trace
 * results.
 */
void trace_line(const struct vec3f *start, const struct vec3f *end,
		uint mask, void *filter, struct trace *tr);

/*
 * Traces a hull/box from start to end, choosing what to collide against based
 * on the provided mask and optionally filtering out collisions using a filter
 * object. The box size is defined by the provided mins and maxs, which are
 * offsets from the start point. tr must point to a valid object and will be
 * filled with the trace results.
 */
void trace_hull(const struct vec3f *start, const struct vec3f *end,
		const struct vec3f *mins, const struct vec3f *maxs, uint mask,
		void *filter, struct trace *tr);

#endif
