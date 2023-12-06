/*
 * Copyright © 2023 Michael Smith <mikesmiffy128@gmail.com>
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

#define _USE_MATH_DEFINES // ... windows.
#include <math.h>

#include "con_.h"
#include "engineapi.h"
#include "errmsg.h"
#include "ent.h"
#include "feature.h"
#include "gamedata.h"
#include "gametype.h"
#include "intdefs.h"
#include "mem.h"
#include "trace.h"
#include "vcall.h"
#include "x86.h"
#include "x86util.h"

FEATURE("Left 4 Dead warp testing")
REQUIRE(ent)
REQUIRE(trace)
REQUIRE_GAMEDATA(off_entpos)
REQUIRE_GAMEDATA(off_eyeang)
REQUIRE_GAMEDATA(off_entteam)
REQUIRE_GAMEDATA(off_entcoll)
REQUIRE_GAMEDATA(vtidx_Teleport)

DECL_VFUNC_DYN(void, Teleport, const struct vec3f */*pos*/,
		const struct vec3f */*pos*/, const struct vec3f */*vel*/)
DECL_VFUNC(const struct vec3f *, OBBMaxs, 2)

DECL_VFUNC_DYN(void, AddLineOverlay, const struct vec3f *,
		const struct vec3f *, int, int, int, bool, float)
DECL_VFUNC_DYN(void, AddBoxOverlay2, const struct vec3f *,
		const struct vec3f *, const struct vec3f *, const struct vec3f *,
		const struct rgba *, const struct rgba *, float)
static const struct rgba
		red_edge = {255, 0, 0, 100},
		red_face = {255, 0, 0, 10},
		light_red_edge = {255, 75, 75, 100},
		light_red_face = {255, 75, 75, 10},
		green_edge = {0, 255, 0, 100},
		green_face = {0, 255, 0, 10},
		light_green_edge = {75, 255, 75, 100},
		light_green_face = {75, 255, 75, 10},
		orange_line = {255, 100, 0, 255},
		cyan_line = {0, 255, 255, 255};
static const struct vec3f zero_qangle = {0, 0, 0};

typedef bool (*EntityPlacementTest_L4D1_func)(void *, const struct vec3f *,
		struct vec3f *, bool, uint, void *);
typedef bool (*EntityPlacementTest_func)(void *, const struct vec3f *,
		struct vec3f *, bool, uint, void *, float);

static EntityPlacementTest_func EntityPlacementTest;
static EntityPlacementTest_L4D1_func EntityPlacementTest_l4d1;
// ABI adapter from L4D1 to L4D2
static bool EntityPlacementTestL4D1(void *ent, const struct vec3f *origin,
		struct vec3f *out, bool drop, uint mask, void *filter, float padsz) {
	return EntityPlacementTest_l4d1(ent, origin, out, drop, mask, filter);
}

// Technically the warp uses a CTraceFilterSkipTeam, not a CTraceFilterSimple.
// It does, however, inherit from the simple filter and run some minor checks
// on top of it. I couldn't find a case where these checks actually mattered
// and, if needed, they could be easily reimplemented using the extra hit check
// (instead of hunting for the CTraceFilterSkipTeam vtable)
static struct filter_simple {
	void **vtable;
	void *pass_ent;
	int collision_group;
	void * /* ShouldHitFunc_t */ extrahitcheck_func;
	// int teamnum; // player's team number. member of CTraceFilterSkipTeam
} filter;

typedef void (*__thiscall CTraceFilterSimple_ctor)(struct filter_simple *this,
		void *pass_ent, int collision_group, void *extrahitcheck_func);
// Trace mask for non-bot survivors. Constant in all L4D versions
static const int player_mask = 0x0201420B;

static void *dbgoverlay;

static void draw_testpos(const struct vec3f *start, const struct vec3f *testpos,
		const struct vec3f *mins, const struct vec3f *maxs) {
	struct trace tr = {0};
	trace_hull(testpos, testpos, mins, maxs, player_mask, &filter, &tr);
	if (tr.frac != 1.0 || tr.allsolid || tr.startsolid) {
		AddBoxOverlay2(dbgoverlay, testpos, mins, maxs, &zero_qangle,
				&light_red_face, &light_red_edge, 1000.0);
		return;
	}
	AddBoxOverlay2(dbgoverlay, testpos, mins, maxs, &zero_qangle,
			&light_green_face, &light_green_edge, 1000.0);
	trace_line(start, testpos, player_mask, &filter, &tr);
	// current knowledge indicates that this should never happen, but it's good
	// issue a warning if the code ever happens to be wrong
	if (__builtin_expect(tr.frac == 1.0 && !tr.allsolid && !tr.startsolid, 0))
		errmsg_warnx("false positive test position %.2f %.2f %.2f", testpos->x,
			testpos->y, testpos->z);
	AddLineOverlay(dbgoverlay, start, &tr.endpos, orange_line.r, orange_line.g,
			orange_line.b, true, 1000.0);
}

static void draw_warp(const struct vec3f *in, const struct vec3f *out,
		const struct vec3f *mins, const struct vec3f *maxs, bool success) {
	const struct vec3f dims = {maxs->x - mins->x, maxs->y - mins->y,
			maxs->z - mins->z};
	int niter = 15, lastiter = 5;
	for (int i = 0; i < 3; i++) {
		if (in->d[i] != out->d[i]) {
			// rounding to avoid floating point errors
			niter = round(in->d[i] - out->d[i]) / dims.d[i];
			lastiter = i*2;
			// note: this should never happen, but on the off-chance the math
			// above bugs, this works as a fail-safe
			if (__builtin_expect(niter == 0, 0)) goto success;
			// warped towards positive axis -> in < out -> niter < 0
			if (niter < 0) {
				lastiter++;
				niter = -niter;
			}
			AddBoxOverlay2(dbgoverlay, in, mins, maxs, &zero_qangle,
					&light_red_face, &light_red_edge, 1000.0);
			goto do_iters;
		}
	}
	// equal coords means that either 0 or max iterations were done
	if (success) {
success:
		AddBoxOverlay2(dbgoverlay, in, mins, maxs, &zero_qangle, &green_face,
				&green_edge, 1000.0);
		return;
	}
	AddBoxOverlay2(dbgoverlay, in, mins, maxs, &zero_qangle, &red_face,
			&red_edge, 1000.0);
do_iters:
	con_msg("Warp result: %d iterations, ", niter); // log part 1
	struct vec3f offset = {0}, testpos = *in;
	while (--niter) {
		for (int d = 0; d < 3; d++) {
			offset.d[d] += dims.d[d];
			testpos.d[d] -= offset.d[d];
			draw_testpos(in, &testpos, mins, maxs);
			testpos.d[d] += 2*offset.d[d];
			draw_testpos(in, &testpos, mins, maxs);
			testpos.d[d] = in->d[d];
		}
	}
	// handle last placement test iteration separately to stop on the right
	// dimension and direction without checking each iteration
	offset.x += dims.x; offset.y += dims.y; offset.z += dims.z;
	int i, d;
	for (i = 0, d = 0; i < lastiter; d += i & 1, i++) {
		testpos.d[d] += (i & 1) ? offset.d[d] : -offset.d[d];
		draw_testpos(in, &testpos, mins, maxs);
		testpos.d[d] = in->d[d];
	}
	// do last box separately to handle colors and log part 2
	testpos.d[d] += (i & 1) ? offset.d[d] : -offset.d[d];
	if (success) {
		AddBoxOverlay2(dbgoverlay, &testpos, mins, maxs, &zero_qangle,
				&green_face, &green_edge, 1000.0);
		AddLineOverlay(dbgoverlay, in, &testpos, cyan_line.r, cyan_line.g,
				cyan_line.b, true, 1000.0);
		con_msg("%c%c, not stuck\n", (i & 1) ? '+' : '-', 'x'+d);
	}
	else {
		draw_testpos(in, &testpos, mins, maxs);
		con_msg("stuck\n");
	}
}

DEF_CCMD_HERE_UNREG(sst_l4d_testwarp, 
		"Simulate a bot warping to you. This copies your crouching stance to "
		"the simulated bot", CON_SERVERSIDE | CON_CHEAT) {
	struct edict *ed = ent_getedict(con_cmdclient + 1);
	if (!ed || !ed->ent_unknown) {
		errmsg_errorx("couldn't access player entity");
		return;
	}
	void *e = ed->ent_unknown;
	// player's mins and maxs change when you go idle, affecting the placement
	// test, and the Teleport call does nothing anyway
	if (mem_loadu32(mem_offset(e, off_entteam)) != 2) { 
		errmsg_errorx("player is not on the Survivor team");
		return;
	}
	struct vec3f *org = mem_offset(e, off_entpos);
	struct vec3f *ang = mem_offset(e, off_eyeang);
	// L4D idle warps go up to 10 units behind yaw, lessening based on pitch.
	float pitch = ang->x * M_PI / 180, yaw = ang->y * M_PI / 180;
	float shift = -10 * cos(pitch);
	struct vec3f outpos, inpos = {org->x + shift * cos(yaw),
			org->y + shift * sin(yaw), org->z};
	filter.pass_ent = e;
	// TODO(autocomplete): suggest arguments on autocomplete
	bool success = true;
	if ((cmd->argc > 1 && !memcmp(cmd->argv[1], "nounstuck", 10)) ||
			!(success = EntityPlacementTest(e, &inpos, &outpos, false,
			player_mask, (void *)&filter, 0.0)))
		outpos = inpos;
	if (cmd->argc > 1 && !memcmp(cmd->argv[1], "drawtest", 9)) {
		const struct vec3f *maxs = OBBMaxs(mem_offset(e, off_entcoll));
		draw_warp(&inpos, &outpos, &(struct vec3f){-16, -16, 0}, maxs, success);
	}
	else {
		Teleport(e, &outpos, 0, &(struct vec3f){0, 0, 0});
	}
}

PREINIT {
	return GAMETYPE_MATCHES(L4D);
}

static void *find_EntityPlacementTest(con_cmdcb z_add_cb) {
	const uchar *insns = (const uchar *)z_add_cb;
	for (const uchar *p = insns; p - insns < 0x300;) {
		// Find 0, 0x200400B and 1 being pushed to the stack
		if (p[0] == X86_PUSHI8 && p[1] == 0 && p[2] == X86_PUSHIW &&
				mem_loadu32(p + 3) == 0x200400B && p[7] == X86_PUSHI8 &&
				p[8] == 1) {
			p += 9;
			// Next call is the one we are looking for
			while (p - insns < 0x300) {
				if (p[0] == X86_CALL) {
					return (void *)(p + 5 + mem_loads32(p + 1));
				}
				NEXT_INSN(p, "EntityPlacementTest function");
			}
			return 0;
		}
		NEXT_INSN(p, "EntityPlacementTest function");
	}
	return 0;
}

static bool init_filter(void *EntityPlacementTest) {
	const uchar *insns = (const uchar *)EntityPlacementTest;
	for (const uchar *p = insns; p - insns < 0x60;) {
		if (p[0] == X86_CALL) {
			CTraceFilterSimple_ctor constructor = (CTraceFilterSimple_ctor)
					(p + 5 + mem_loads32(p + 1));
			// calling the constructor to fill the vtable and other members
			// with values used by the engine. pass_ent is filled in runtime
			constructor(&filter, 0, 8, 0);
			return true;
		}
		NEXT_INSN(p, "CTraceFilterSimple constructor");
	}
	return false;
}

INIT {
	if (!(dbgoverlay = factory_engine("VDebugOverlay003", 0))) {
		errmsg_warnx("no debug overlay");
		return false;
	}
	struct con_cmd *z_add = con_findcmd("z_add");
	void *func;
	if (!z_add || !(func = find_EntityPlacementTest(z_add->cb))) {
		errmsg_errorx("couldn't find EntityPlacementTest function");
		return false;
	}
	if (!init_filter(func)) {
		errmsg_errorx("couldn't init trace filter for EntityPlacementTest");
		return false;
	}
	if (GAMETYPE_MATCHES(L4D1)) {
		EntityPlacementTest_l4d1 = (EntityPlacementTest_L4D1_func)func;
		EntityPlacementTest = &EntityPlacementTestL4D1;
	}
	else {
		EntityPlacementTest = (EntityPlacementTest_func)func;
	}
	con_reg(sst_l4d_testwarp);
	return true;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
