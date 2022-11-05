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

// windows api
#ifdef CreateFont
#undef CreateFont
#endif

struct hscheme { ulong handle; };
enum fontdrawtype {
	FONT_DRAW_DEFAULT = 0,
	FONT_DRAW_NONADDITIVE,
	FONT_DRAW_ADDITIVE,
	FONT_DRAW_TYPE_COUNT = 2,
};

FEATURE("hud painting")
REQUIRE_GLOBAL(factory_engine)
// ISurface
REQUIRE_GAMEDATA(vtidx_DrawSetColor)
REQUIRE_GAMEDATA(vtidx_DrawFilledRect)
REQUIRE_GAMEDATA(vtidx_DrawOutlinedRect)
REQUIRE_GAMEDATA(vtidx_DrawLine)
REQUIRE_GAMEDATA(vtidx_DrawPolyLine)
REQUIRE_GAMEDATA(vtidx_DrawSetTextFont)
REQUIRE_GAMEDATA(vtidx_DrawSetTextColor)
REQUIRE_GAMEDATA(vtidx_DrawSetTextPos)
REQUIRE_GAMEDATA(vtidx_DrawPrintText)
REQUIRE_GAMEDATA(vtidx_GetScreenSize)
REQUIRE_GAMEDATA(vtidx_GetFontTall)
REQUIRE_GAMEDATA(vtidx_CreateFont)
REQUIRE_GAMEDATA(vtidx_SetFontGlyphSet)
REQUIRE_GAMEDATA(vtidx_GetCharacterWidth)
// CEngineVGui
REQUIRE_GAMEDATA(off_engineToolsPanel)
// vgui::Panel
REQUIRE_GAMEDATA(vtidx_SetPaintEnabled)
REQUIRE_GAMEDATA(vtidx_Paint)
// ISchemeManager
REQUIRE_GAMEDATA(vtidx_GetScheme)
REQUIRE_GAMEDATA(vtidx_GetIScheme)
// IScheme
REQUIRE_GAMEDATA(vtidx_GetFont)

DEF_EVENT(HudPaint)

// ISurface
DECL_VFUNC_DYN(void, DrawSetColor, struct rgba_colour)
DECL_VFUNC_DYN(void, DrawFilledRect, int, int, int, int)
DECL_VFUNC_DYN(void, DrawOutlinedRect, int, int, int, int)
DECL_VFUNC_DYN(void, DrawLine, int, int, int, int)
DECL_VFUNC_DYN(void, DrawPolyLine, int *, int *, int)
DECL_VFUNC_DYN(void, DrawSetTextFont, struct hfont)
DECL_VFUNC_DYN(void, DrawSetTextColor, struct rgba_colour)
DECL_VFUNC_DYN(void, DrawSetTextPos, int, int)
DECL_VFUNC_DYN(void, DrawPrintText, ushort *, int, enum fontdrawtype)
DECL_VFUNC_DYN(void, GetScreenSize, int *, int *)
DECL_VFUNC_DYN(int, GetFontTall, struct hfont)
DECL_VFUNC_DYN(struct hfont, CreateFont)
DECL_VFUNC_DYN(bool, SetFontGlyphSet, struct hfont, const char *, int, int,
		int, int, int, int, int)
DECL_VFUNC_DYN(int, GetCharacterWidth, struct hfont, int)
// vgui::Panel
DECL_VFUNC_DYN(void, SetPaintEnabled, bool)
// ISchemeManager
DECL_VFUNC_DYN(struct hscheme, GetScheme, const char *)
DECL_VFUNC_DYN(void*, GetIScheme, struct hscheme)
// IScheme
DECL_VFUNC_DYN(struct hfont, GetFont, const char *, bool)

static void *mss;

static void *toolspanel;
typedef void (*VCALLCONV Paint_func)(void *);
Paint_func orig_Paint;

static void *scheme;
struct hfont hud_getfont(const char *name, bool proportional) {
	return GetFont(scheme, name, proportional);
}

void hud_drawrect(int x0, int y0, int x1, int y1, struct rgba_colour colour,
		bool filled) {
	DrawSetColor(mss, colour);
	if (filled) DrawFilledRect(mss, x0, y0, x1, y1);
	else DrawOutlinedRect(mss, x0, y0, x1, y1);
}

void hud_drawline(int x0, int y0, int x1, int y1, struct rgba_colour colour) {
	DrawSetColor(mss, colour);
	DrawLine(mss, x0, y0, x1, y1);
}

void hud_drawpolyline(int *x, int *y, int num_points) {
	DrawPolyLine(mss, x, y, num_points);
}

void hud_drawtext(struct hfont font, int x, int y, struct rgba_colour colour,
		ushort *str, size_t len) {
	DrawSetTextFont(mss, font);
	DrawSetTextPos(mss, x, y);
	DrawSetTextColor(mss, colour);
	DrawPrintText(mss, str, len, FONT_DRAW_DEFAULT);
}

void hud_getscreensize(int *width, int *height) {
	GetScreenSize(mss, width, height);
}

int hud_getfonttall(struct hfont font) {
	return GetFontTall(mss, font);
}

int hud_getcharwidth(struct hfont font, int ch) {
	return GetCharacterWidth(mss, font, ch);
}

struct hfont hud_createfont(const char *fontname, int tall, int weight,
		int blur, int scanlines, int flags) {
	struct hfont font = CreateFont(mss);
	if (SetFontGlyphSet(mss, font, fontname, tall, weight, blur, scanlines, flags,
			0, 0)) return font;
	return (struct hfont){0};
}

void VCALLCONV hook_Paint(void *this) {
	if (this == toolspanel) {
		EMIT_HudPaint();
	}
	orig_Paint(this);
}

INIT {
	mss = factory_engine("MatSystemSurface006", 0);
	void *enginevgui = factory_engine("VEngineVGui001", 0);
	void *schememgr = factory_engine("VGUI_Scheme010", 0);
	if (!(enginevgui && mss && schememgr)) {
		errmsg_errorx("couldn't get interfaces");
		return false;
	}
	toolspanel = mem_loadptr(mem_offset(enginevgui, off_engineToolsPanel));
	void **vtable = *(void***)toolspanel;
	if (!os_mprot(vtable + vtidx_Paint, sizeof(void *),
			PAGE_READWRITE)) {
		errmsg_errorsys("couldn't make virtual table writable");
		return false;
	}
	orig_Paint = (Paint_func)hook_vtable(vtable, vtidx_Paint,
			(void *)&hook_Paint);
	SetPaintEnabled(toolspanel, true);
	// 1 is the default, first loaded scheme. should always be sourcescheme.res
	scheme = GetIScheme(schememgr, (struct hscheme){1});
	return true;
}

END {
	unhook_vtable(*(void***)toolspanel, vtidx_Paint, (void*)orig_Paint);
	SetPaintEnabled(toolspanel, false);
}

// vi: sw=4 ts=4 noet tw=80 cc=80