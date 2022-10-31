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
#include "event.h"
#include "feature.h"
#include "gamedata.h"
#include "hook.h"
#include "hud.h"
#include "mem.h"
#include "os.h"
#include "vcall.h"

FEATURE("hud painting")

REQUIRE_GAMEDATA(vtidx_DrawPrintText)
REQUIRE_GAMEDATA(vtidx_Paint)
REQUIRE_GAMEDATA(vtidx_SetPaintEnabled)
REQUIRE_GAMEDATA(vtidx_GetScheme)
REQUIRE_GAMEDATA(vtidx_GetIScheme)
REQUIRE_GAMEDATA(vtidx_GetFont)
REQUIRE_GAMEDATA(off_engineToolsPanel)
REQUIRE_GLOBAL(factory_engine)

enum FontDrawType {
	FONT_DRAW_DEFAULT = 0,
	FONT_DRAW_NONADDITIVE,
	FONT_DRAW_ADDITIVE,
	FONT_DRAW_TYPE_COUNT = 2,
};

typedef ulong HScheme;

DEF_EVENT(HudPaint)

// ISurface
DECL_VFUNC_DYN(void, DrawSetTextPos, int, int)
DECL_VFUNC_DYN(void, DrawSetTextColor, int, int, int, int)
DECL_VFUNC_DYN(void, DrawSetTextFont, HFont)
DECL_VFUNC_DYN(void, DrawPrintText, ushort *, int, enum FontDrawType)
// vgui::Panel
DECL_VFUNC_DYN(void, SetPaintEnabled, bool)
// ISchemeManager
DECL_VFUNC_DYN(HScheme, GetScheme, const char *)
DECL_VFUNC_DYN(void*, GetIScheme, HScheme)
// IScheme
DECL_VFUNC_DYN(HFont, GetFont, const char *, bool)

static void *matsyssurf;
static void *scheme;
static void *toolspanel;
static void **vtable;

typedef void (*VCALLCONV Paint_func)(void *);
Paint_func orig_Paint;

HFont hud_getfont(const char *name, bool proportional) {
	return GetFont(scheme, name, proportional);
}

void hud_drawtext(HFont font, int x, int y, struct Color color, ushort *str,
		size_t len) {
	DrawSetTextFont(matsyssurf, font);
	DrawSetTextPos(matsyssurf, x, y);
	DrawSetTextColor(matsyssurf, color.r, color.g, color.b, color.a);
	DrawPrintText(matsyssurf, str, len, FONT_DRAW_DEFAULT);
}

void VCALLCONV hook_Paint(void *this) {
	if (this == toolspanel) {
		EMIT_HudPaint();
	}
	orig_Paint(this);
}

PREINIT {
	return true;
}

INIT {
	matsyssurf = factory_engine("MatSystemSurface006", 0);
	void *enginevgui = factory_engine("VEngineVGui001", 0);
	void *schememgr = factory_engine("VGUI_Scheme010", 0);
	if (!(enginevgui && matsyssurf && schememgr)) {
		errmsg_errorx("couldn't get interfaces");
		return false;
	}
	scheme = GetIScheme(schememgr, 1); // 1 is default scheme

	toolspanel = mem_loadptr(mem_offset(enginevgui, off_engineToolsPanel));
	vtable = *(void***)toolspanel;
	if (!os_mprot(vtable + vtidx_Paint, sizeof(void *),
			PAGE_READWRITE)) {
		errmsg_errorsys("couldn't make virtual table writable");
		return false;
	}
	orig_Paint = (Paint_func)hook_vtable(vtable, vtidx_Paint,
			(void *)&hook_Paint);
	SetPaintEnabled(toolspanel, true);

	return true;
}

END {
	unhook_vtable(vtable, vtidx_Paint, (void*)orig_Paint);
	SetPaintEnabled(toolspanel, false);
}

// vi: sw=4 ts=4 noet tw=80 cc=80
