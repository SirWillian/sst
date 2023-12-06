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

#include "engineapi.h"
#include "errmsg.h"
#include "feature.h"
#include "gametype.h"
#include "intdefs.h"
#include "trace.h"

FEATURE()
// TODO: provide basic/common filters from this feature

struct ray {
	// these are actually VectorAligned in engine code, which occupy 16 bytes
	// and are forcefully aligned as such
	struct vec3f __attribute__((aligned(16))) start, delta, startoff, extents;
	// align to 16 since "extents" is supposed to occupy 16 bytes.
    // TODO(compat): this member isn't in every engine branch
	const float __attribute__((aligned(16))) (*worldaxistransform)[3][4];
	bool isray, isswept;
};

DECL_VFUNC(void, TraceRay, 5, struct ray *, uint /*mask*/, void */*filter*/,
		struct trace *)
static void *srvtrace;

void trace_line(const struct vec3f *start, const struct vec3f *end,
		uint mask, void *filter, struct trace *tr) {
	struct ray ray = {.start = *start, .startoff = {0}, .extents = {0},
			.worldaxistransform = 0, .isray = true};
	ray.delta.x = end->x - start->x;
	ray.delta.y = end->y - start->y;
	ray.delta.z = end->z - start->z;
	// game calculates this as m_Delta.LengthSqr() != 0
	ray.isswept = ray.delta.x != 0 || ray.delta.y != 0 || ray.delta.z != 0;
	TraceRay(srvtrace, &ray, mask, filter, tr);
}

void trace_hull(const struct vec3f *start, const struct vec3f *end,
		const struct vec3f *mins, const struct vec3f *maxs, uint mask,
		void *filter, struct trace *tr) {
	struct ray ray = {.start = *start, .worldaxistransform = 0};
	ray.delta.x = end->x - start->x;
	ray.delta.y = end->y - start->y;
	ray.delta.z = end->z - start->z;
	// game calculates this as m_Delta.LengthSqr() != 0
	ray.isswept = ray.delta.x != 0 || ray.delta.y != 0 || ray.delta.z != 0;
	ray.extents.x = (maxs->x - mins->x) * 0.5;
	ray.extents.y = (maxs->y - mins->y) * 0.5;
	ray.extents.z = (maxs->z - mins->z) * 0.5;
    // XXX: could probably hardcode this to false, but it's probably better to
    // copy engine logic in the off chance we trace some insanely thin hull
	ray.isray = (ray.extents.x * ray.extents.x + ray.extents.y * ray.extents.y +
			ray.extents.z * ray.extents.z) < 1e-6;
	// center start vector and make offset point towards the original start
	ray.startoff.x = -0.5 * (mins->x + maxs->x);
	ray.startoff.y = -0.5 * (mins->y + maxs->y);
	ray.startoff.z = -0.5 * (mins->z + maxs->z);
	ray.start.x -= ray.startoff.x;
	ray.start.y -= ray.startoff.y;
	ray.start.z -= ray.startoff.z;
	TraceRay(srvtrace, &ray, mask, filter, tr);
}

PREINIT {
    // TODO(compat): restricting it to tested branches for now
    return GAMETYPE_MATCHES(L4D);
}

INIT {
    if (!(srvtrace = factory_engine("EngineTraceServer003", 0))) {
		errmsg_errorx("couldn't get server-side tracing interface");
		return false;
	}
    return true;
}
