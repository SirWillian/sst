#include "con_.h"
#include "engineapi.h"
#include "errmsg.h"
#include "feature.h"
#include "hook.h"
#include "mem.h"
#include "sst.h"
#include "x86.h"
#include "x86util.h"
#include <winnt.h>

FEATURE("usercmd test")
REQUIRE_GLOBAL(factory_server)

struct CUserCmd {
	void **vtable;
	int cmd;
	int tick;
	struct vec3f angles;
	float fmove;
	float smove;
	float umove;
	int buttons;
	char impulse;
	int weaponselect;
	int weaponsubtype;
	int random_seed;
	short mousedx;
	short mousedy;
	// client only!!
	bool predicted;
	struct CUtlVector *entitygroundcontact;
};

static void **vtable;
static const int vtidx_Processusercmds = 9;
static const int vtidx_GetMaxSplitscreenPlayers = 17;

typedef float (*VCALLCONV Processusercmds_func)(void *this, struct edict *player, void /*bf_read*/ *buf,
	int numcmds, int totalcmds, int dropped_packets, bool ignore, bool paused);
static Processusercmds_func orig_Processusercmds;

static float VCALLCONV hook_Processusercmds(void *this, struct edict *player,
	    void *buf, int numcmds, int totalcmds, int dropped_packets, bool ignore, bool paused) {
    con_msg("Received %d new UserCmds, %d cmds in packet, %d dropped, ignore %d and paused %d\n",
        numcmds, totalcmds, dropped_packets, ignore, paused);
	return orig_Processusercmds(this, player, buf, numcmds, totalcmds, dropped_packets, ignore, paused);
}

typedef int (*VCALLCONV GetMaxSplitscreenPlayers_func)(void);
static GetMaxSplitscreenPlayers_func orig_GetMaxSplitscreenPlayers;
static int VCALLCONV hook_GetMaxSplitscreenPlayers(void) {
	return 4;
}

typedef void (*__cdecl ReadUsercmd_func)(void /*bf_read*/ *buf, struct CUserCmd *to, struct CUserCmd *from);
static ReadUsercmd_func orig_ReadUsercmd = 0;
static void __cdecl hook_ReadUsercmd(void *buf, struct CUserCmd *to, struct CUserCmd *from) {
    orig_ReadUsercmd(buf, to, from);
    con_msg("\tUserCmd on tick %d\n", to->tick);
}

static bool find_ReadUsercmd(void *func) {
    const struct rgba nice_colour = {40, 160, 140, 255}; // a nice teal colour
    const uchar *insns = (const uchar*)func;
    int callcount = 0, _len = -1;
	for (const uchar *p = insns; p - insns < 512;) {
        if (p[0] == X86_CALL) {
            callcount++;
            if (callcount == 3)
                orig_ReadUsercmd = (ReadUsercmd_func)(p + 5 + mem_loadoffset(p + 1));
        }
        _len = x86_len(p);
        
        if (_len == -1) {
            errmsg_errorx("unknown or invalid instruction looking for ReadUsercmd function");
            return false;
        }
        con_colourmsg(&nice_colour, "(%p)", (void *)p);
        for (int i = 0; i < _len; i++) con_colourmsg(&nice_colour, " %02x", p[i]);
        con_msg("\n");
        if (orig_ReadUsercmd != 0) {
            int offset = mem_loadoffset(p + 1);
            con_msg("p %p offset %d final addr %p\n", p, offset, p + 5 + offset);
            return true;
        }
        (p) += _len;
    }
    return false;
}

INIT {
	void *svgameclients = factory_server("ServerGameClients003", 0);
	if (!svgameclients) {
		errmsg_errorx("couldn't get server game clients interface");
		return false;
	}

	vtable = *(void***)svgameclients;
	// if (!find_ReadUsercmd(vtable[vtidx_Processusercmds])) {
	// 	errmsg_errorx("couldn't find ReadUsercmd func");
	// 	return false;
	// }
	if (!os_mprot(vtable + vtidx_Processusercmds, sizeof(void *)*8,
			PAGE_READWRITE)) {
		errmsg_errorx("couldn't make virtual table writable");
		return false;
	}

	// void *campaignendvtfunc = (uchar *)clientlib + 0x4fe5c0;
	// if (!os_mprot(campaignendvtfunc, sizeof(void *), PAGE_READWRITE)) {
	// 	errmsg_errorx("couldn't make virtual table writable");
	// 	return false;
	// }

	// orig_Processusercmds = (Processusercmds_func)hook_vtable(vtable, vtidx_Processusercmds, (void *)&hook_Processusercmds);
	// con_msg("Processusercmds %p ReadUsercmd %p\n", orig_Processusercmds, orig_ReadUsercmd);
	// orig_ReadUsercmd = (ReadUsercmd_func)hook_inline((void *)orig_ReadUsercmd, (void *)&hook_ReadUsercmd);

	orig_GetMaxSplitscreenPlayers = (GetMaxSplitscreenPlayers_func)hook_vtable(vtable, vtidx_GetMaxSplitscreenPlayers, (void *)&hook_GetMaxSplitscreenPlayers);
	con_msg("old maxsplitscreen %d new %d\n", orig_GetMaxSplitscreenPlayers(), hook_GetMaxSplitscreenPlayers());
	return true;
}

END {
    //unhook_vtable(vtable, vtidx_Processusercmds, (void *)orig_Processusercmds);
	unhook_vtable(vtable, vtidx_GetMaxSplitscreenPlayers, (void *)orig_GetMaxSplitscreenPlayers);
    //unhook_inline((void *)orig_ReadUsercmd);
}
