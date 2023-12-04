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
#include "vcall.h"
#include "x86.h"
#include "x86util.h"

FEATURE("Left 4 Dead warp testing")
REQUIRE(ent)
REQUIRE_GAMEDATA(off_entpos)
REQUIRE_GAMEDATA(off_eyeang)
REQUIRE_GAMEDATA(off_entteam)
REQUIRE_GAMEDATA(vtidx_Teleport)

DECL_VFUNC_DYN(void, Teleport, const struct vec3f */*pos*/,
		const struct vec3f */*pos*/, const struct vec3f */*vel*/)

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
	if ((cmd->argc > 1 && !memcmp(cmd->argv[1], "nounstuck", 10)) ||
			!EntityPlacementTest(e, &inpos, &outpos, false, player_mask,
			(void *)&filter, 0.0))
		outpos = inpos;
	Teleport(e, &outpos, 0, &(struct vec3f){0, 0, 0});
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
