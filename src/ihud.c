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

#include "con_.h"
#include "engineapi.h"
#include "errmsg.h"
#include "feature.h"
#include "gamedata.h"
#include "hook.h"
#include "hud.h"
#include "mem.h"
#include "os.h"
#include "vcall.h"
#include "x86.h"
#include "x86util.h"

FEATURE("input hud")
REQUIRE_GAMEDATA(vtidx_CreateMove)
REQUIRE_GAMEDATA(vtidx_DecodeUserCmdFromBuffer)
REQUIRE_GAMEDATA(vtidx_GetUserCmd)
REQUIRE_GAMEDATA(vtidx_VClient_DecodeUserCmdFromBuffer)
REQUIRE_GLOBAL(factory_client)
REQUIRE(hud)

enum incode {
	IN_NONE = 0,
	IN_ATTACK = (1 << 0),
	IN_JUMP = (1 << 1),
	IN_DUCK = (1 << 2),
	IN_FORWARD = (1 << 3),
	IN_BACK = (1 << 4),
	IN_USE = (1 << 5),
	IN_CANCEL = (1 << 6),
	IN_LEFT = (1 << 7),
	IN_RIGHT = (1 << 8),
	IN_MOVELEFT = (1 << 9),
	IN_MOVERIGHT = (1 << 10),
	IN_ATTACK2 = (1 << 11),
};

struct usercmd {
	void **vtable;
	int cmd;
	int tick;
	struct vec3f angles;
	int fmove;
	int smove;
	int umove;
	int buttons;
	int impulse;
	int weaponselect;
	int weaponsubtype;
	int random_seed;
	short mousedx;
	short mousedy;
	// client only!!
	bool predictedv;
};

DECL_VFUNC_DYN(struct usercmd *, GetUserCmd, int)

typedef void (*VCALLCONV CreateMove_func)(void *, int, float, bool);
typedef void (*VCALLCONV DecodeUserCmdFromBuffer_func)(void *, void *, int);

CreateMove_func orig_CreateMove;
DecodeUserCmdFromBuffer_func orig_DecodeUserCmdFromBuffer;
void *input = 0;
struct hfont font;
int buttons;

struct key {
	char x;
	char y;
	char w;
	char h;
	ushort ch;
	enum incode button;
};

static int padding = 5;
static int size = 60;
static int corner = 3;
static int layout = 0;
static struct con_colour pressed = {128, 128, 128, 200};
static struct con_colour unpressed = {0, 0, 0, 92};
// default layout
static struct key layouts[1][10] = {
	{
		{0, 0, 1, 1, L'C', IN_DUCK},
		{1, 0, 3, 1, L'J', IN_JUMP},
		{4, 0, 1, 1, L'L', IN_ATTACK},
		{5, 0, 1, 1, L'R', IN_ATTACK2},
		{1, 1, 1, 1, L'A', IN_MOVELEFT},
		{2, 1, 1, 1, L'S', IN_BACK},
		{3, 1, 1, 1, L'D', IN_MOVERIGHT},
		{2, 2, 1, 1, L'W', IN_FORWARD},
		{3, 2, 1, 1, L'E', IN_USE},
		{0, 0, 0, 0, 0, IN_NONE},
	},
};

int w;
int h;
HANDLE_EVENT(HudPaint) {
	hud_getscreensize(&w, &h);
	for (struct key *k = layouts[layout]; k->button; k++) {
		struct con_colour colour = buttons & k->button ? pressed : unpressed;
		int x0 = size * k->x + padding * (k->x+1);
		int y0 = h - size * (k->y+1) - padding * (k->y+1);
		int x1 = x0 + size * k->w + padding * (k->w-1);
		int y1 = y0 + size * k->h + padding * (k->h-1);
		hud_drawrect(x0, y0, x1, y1, colour, true);
		int tx = x1 - (x1 - x0) / 2 - hud_getcharwidth(font, k->ch) / 2;
		int ty = y1 - (y1 - y0) / 2 - hud_getfonttall(font) / 2;
		hud_drawtext(font, tx, ty, (struct con_colour){255, 255, 255, 255},
				&k->ch, 1);
	};
}

// find the CInput "input" global
static inline bool find_input(void* vclient) {
#ifdef _WIN32
	// the only CHLClient::DecodeUserCmdFromBuffer does is call a virtual 
	// function, so find it's thisptr being loaded into ECX
	void* decodeusercmd =
		(*(void***)vclient)[vtidx_VClient_DecodeUserCmdFromBuffer];
	errmsg_errorx("%x", (uint)decodeusercmd);
	for (uchar *p = (uchar *)decodeusercmd; p - (uchar *)decodeusercmd < 32;) {
		if (p[0] == X86_MOVRMW && p[1] == X86_MODRM(0, 1, 5)) {
			void **indirect = mem_loadptr(p + 2);
			input = *indirect;
			return true;
		}
		NEXT_INSN(p, "input object");
	}
#else
#warning TODO(linux): implement linux equivalent (see demorec.c)
#endif
	return false;
}

void VCALLCONV hook_CreateMove(void *this, int seq, float ft, bool active) {
	orig_CreateMove(this, seq, ft, active);
	buttons = GetUserCmd(this, seq)->buttons;
}

void VCALLCONV hook_DecodeUserCmdFromBuffer(void *this, void *reader, int seq) {
	// dumb hack to get the correct pointer
	orig_CreateMove(this, seq, 0.015, true);
	struct usercmd *cmd = GetUserCmd(this, seq);
	orig_DecodeUserCmdFromBuffer(this, reader, seq);
	buttons = cmd->buttons;
}


INIT {
	void *vclient = factory_client("VClient015", 0);
	if (!vclient) {
		errmsg_errorx("couldn't get client interface");
		return false;
	}
	if (!find_input(vclient)) {
		errmsg_errorx("couldn't find cinput global");
		return false;
	}
	void **vtable = *(void ***)input;
	// just unprotect the first few pointers (getusercmd is 8)
	if (!os_mprot(vtable, sizeof(void *) * 8, PAGE_READWRITE)) { 
		errmsg_errorsys("couldn't make virtual table writable");
		return false;
	}
	font = hud_getfont("CloseCaption_Normal", false);
	if (!font.handle) {
		errmsg_errorx("couldn't get font");
		return false;
	}
	orig_CreateMove = (CreateMove_func)hook_vtable(vtable, vtidx_CreateMove,
			(void *)hook_CreateMove);
	orig_DecodeUserCmdFromBuffer = (DecodeUserCmdFromBuffer_func)hook_vtable(
			vtable, vtidx_DecodeUserCmdFromBuffer,
			(void *)hook_DecodeUserCmdFromBuffer);
	return true;
}

END {
	void **vtable = *(void ***)input;
	unhook_vtable(vtable, vtidx_CreateMove, (void *)orig_CreateMove);
	unhook_vtable(vtable, vtidx_DecodeUserCmdFromBuffer,
			(void *)orig_DecodeUserCmdFromBuffer);
}

// vi: sw=4 ts=4 noet tw=80 cc=80
