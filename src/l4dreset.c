/*
 * Copyright © 2023 Willian Henrique <wsimanbrazil@yahoo.com.br>
 * Copyright © 2023 Michael Smith <mikesmiffy128@gmail.com>
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

#include <string.h>

#include "con_.h"
#include "engineapi.h"
#include "ent.h"
#include "errmsg.h"
#include "event.h"
#include "fastfwd.h"
#include "feature.h"
#include "gamedata.h"
#include "gametype.h"
#include "hook.h"
#include "intdefs.h"
#include "l4dmm.h"
#include "mem.h"
#include "sst.h"
#include "vcall.h"
#include "x86.h"
#include "x86util.h"

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

FEATURE("Left 4 Dead quick resetting")
REQUIRE(ent)
REQUIRE(fastfwd)
REQUIRE(l4dmm)
REQUIRE_GLOBAL(srvdll)
REQUIRE_GAMEDATA(vtidx_GameFrame) // note: for L4D1 only, always defined anyway
REQUIRE_GAMEDATA(vtidx_GameShutdown)
REQUIRE_GAMEDATA(vtidx_OnGameplayStart)

static void **votecontroller;
static int off_callerrecords = -1;
static int off_voteissues;

static void *director; // "TheDirector" server global

// Note: the vote callers vector contains these as elements. We don't currently
// do anything with the structure, but we're keeping it here for reference.
/*struct CallerRecord {
	u32 steamid_trunc;
	float last_time;
	int votes_passed;
	int votes_failed;
	int last_issueidx;
	bool last_passed;
};*/

// XXX: duping this again here... what makes sense to tidy this up?
#ifdef _WIN32
#define NVDTOR 1
#else
#define NVDTOR 2
#endif

struct CVoteIssue;
DECL_VFUNC(const char *, SetIssueDetails, 1 + NVDTOR, const char *)
DECL_VFUNC(const char *, GetDisplayString, 8 + NVDTOR)
DECL_VFUNC(const char *, ExecuteCommand, 9 + NVDTOR)

static struct CVoteIssue *getissue(const char *textkey) {
	struct CUtlVector *issuevec = mem_offset(*votecontroller, off_voteissues);
	struct CVoteIssue **issues = issuevec->m.mem;
	for (int i = 0; /*i < issuevec->sz*/; ++i) { // key MUST be valid!
		if (!strcmp(GetDisplayString(issues[i]), textkey)) return issues[i];
	}
}

static inline void reset(void) {
	// reset the vote cooldowns if possible (will skip L4D1). only necessary on
	// versions >2045 and on map 1, but it's easiest to do unconditionally
	if (off_callerrecords != -1) {
		// This is equivalent to CUtlVector::RemoveAll() as there's no
		// destructors to call. The result as is if nobody had ever voted.
		struct CUtlVector *recordvector = mem_offset(*votecontroller,
				off_callerrecords);
		recordvector->sz = 0;
	}
	ExecuteCommand(getissue("#L4D_vote_restart_game"));
}

static inline void change(const char *missionid) {
	struct CVoteIssue *issue = getissue("#L4D_vote_mission_change");
	SetIssueDetails(issue, missionid); // will just nop if invalid
	ExecuteCommand(issue);
}

static short ffdelay = 0, ffamt;
static bool mapchanging = false;

HANDLE_EVENT(Tick, bool simulating) {
	if (!mapchanging && ffdelay && !--ffdelay) fastfwd(ffamt, 30);
}

typedef void (*VCALLCONV OnGameplayStart_func)(void *this);
static OnGameplayStart_func orig_OnGameplayStart;
static void VCALLCONV hook_OnGameplayStart(void *this) {
	orig_OnGameplayStart(this);
	if (mapchanging) reset(); // prevent bots walking around. note ffdelay is 45
	mapchanging = false; // resume countdown!
}
// Simply reuse the above for L4D1, since the calling ABI is the exact same!
#define UnfreezeTeam_func OnGameplayStart_func
#define UnfreezeTeam OnGameplayStart
#define orig_UnfreezeTeam orig_OnGameplayStart
#define hook_UnfreezeTeam hook_OnGameplayStart

static inline int getffamt(const char *campaign) {
	// TODO(compat): add popular custom campaigns too:
	// - dark blood 2
	// - grey scale
	// - left 4 mario
	// - ravenholm
	// - warcelona
	// - tour of terror
	// - dam it
	// - carried off
	if (GAMETYPE_MATCHES(L4D1)) {
		if (!strcmp(campaign, "Hospital")) return 9; // No Mercy
		if (!strcmp(campaign, "SmallTown")) return 12; // Death Toll
		if (!strcmp(campaign, "Airport")) return 12; // Dead Air
		if (!strcmp(campaign, "Farm")) return 15; // Blood Harvest
		if (!strcmp(campaign, "River")) return 15; // The Sacrifice
		if (!strcmp(campaign, "Garage")) return 8; // Crash Course
	}
	else /* L4D2 */ {
		if (!strncmp(campaign, "L4D2C", 5)) {
			int ret;
			switch (campaign[5]) {
				case '1':
					switch (campaign[6]) {
						case '\0': return 13; // Dead Center
						case '0': ret = 12; break; // Death Toll
						case '1': ret = 13; break; // Dead Air
						case '2': ret = 16; break; // Blood Harvest
						case '3': ret = 18; break; // Cold Stream
						case '4': ret = 16; break; // The Last Stand
						default: return 0;
					}
					if (campaign[7]) return 0;
					return ret;
				case '2': ret = 12; break; // Dark Carnival
				case '3': ret = 19; break; // Swamp Fever
				case '4': ret = 8; break; // Hard Rain
				case '5': ret = 13; break; // The Parish
				case '6': ret = 10; break; // The Passing
				case '7': ret = 15; break; // The Sacrifice
				case '8': ret = 9; break; // No Mercy
				case '9': ret = 8; break; // Crash Course
				default: return 0;
			}
			if (campaign[6]) return 0;
			return ret;
		}
	}
	return 0; // if unknown, just don't skip, I guess.
}

DEF_CVAR_UNREG(sst_l4d_quickreset_fastfwd,
		"Fast-forward through cutscenes when quick-resetting", 1,
		CON_ARCHIVE | CON_HIDDEN)

DEF_CCMD_HERE_UNREG(sst_l4d_quickreset,
		"Reset (or switch) campaign and clear all vote cooldowns", 0) {
	if (cmd->argc > 2) {
		con_warn("usage: sst_l4d_quickreset [campaignid]\n");
		return;
	}
	if (!*votecontroller) {
		con_warn("not hosting a server\n");
		return;
	}
	const char *campaign = l4dmm_curcampaign();
	if (cmd->argc == 2 && (!campaign || strcasecmp(campaign, cmd->argv[1]))) {
		change(cmd->argv[1]);
		campaign = cmd->argv[1];
		mapchanging = true;
	}
	else {
		reset();
		// same-map reset is delayed by about a second - save that time here
		// also, set mapchanging back to false in case it got stuck somehow
		if (!(mapchanging = !l4dmm_firstmap())) fastfwd(0.8, 10);
	}
	if (campaign && con_getvari(sst_l4d_quickreset_fastfwd) &&
			(ffamt = getffamt(campaign))) {
		ffdelay = 45; // 1.5s
	}
}

PREINIT {
	if (!GAMETYPE_MATCHES(L4D)) return false;
	con_reg(sst_l4d_quickreset_fastfwd);
	return true;
}

// Note: this returns a pointer to subsequent bytes for find_voteissues() below
static inline const uchar *find_votecontroller(con_cmdcbv1 listissues_cb) {
	const uchar *insns = (const uchar *)listissues_cb;
#ifdef _WIN32
	// The "listissues" command calls CVoteController::ListIssues, loading
	// g_voteController into ECX
	for (const uchar *p = insns; p - insns < 32;) {
		if (p[0] == X86_MOVRMW && p[1] == X86_MODRM(0, 1, 5)) {
			votecontroller = mem_loadptr(p + 2);
			return p;
		}
		NEXT_INSN(p, "g_voteController variable");
	}
#else
#warning TODO(linux): this will be different
#endif
	return 0;
}

static inline bool find_voteissues(const uchar *insns) {
#ifdef _WIN32
	for (const uchar *p = insns; p - insns < 16;) {
		// Look for the last call before the ret - that has to be ListIssues()
		if (p[0] == X86_CALL && p[5] == X86_RET) {
			insns = p + 5 + mem_loadoffset(p + 1);
			goto ok;
		}
		NEXT_INSN(p, "ListIssues call");
	}
	return false;
ok:	for (const uchar *p = insns; p - insns < 96;) {
		// The loop in ListIssues() calls a member function on each CVoteIssue.
		// Each pointer is loaded from a CUtlVector at an offset from `this`, so
		// we can find that offset from the mov into ECX.
		if (p[0] == X86_MOVRMW && (p[1] & 0xF8) == 0x88) {
			int off = mem_loadoffset(p + 2);
			if (off > 800) { // sanity check: offset is always fairly high
				off_voteissues = off;
				return true;
			}
		}
		// Complication: at least 2045 has a short jmp over some garbage bytes.
		// Follow that jmp since there's nothing interesting before the target.
		if (p[0] == X86_JMPI8) {
			p += 2 + ((s8 *)p)[1];
			continue;
		}
		NEXT_INSN(p, "offset to vote issue vector");
	}
#else
#warning TODO(linux): and also this
#endif
	return false;
}

static inline bool find_votecallers(void *votectrlspawn) {
#ifdef _WIN32
	const uchar *insns = (const uchar *)votectrlspawn;
	for (const uchar *p = insns; p - insns < 64;) {
		// Unsure what this offset points at (it seems to have to be 0 for votes
		// to happen), but the vector of interest always comes 8 bytes later.
		// "mov dword ptr [<reg> + off], 0", mod == 0b11
		if (p[0] == X86_MOVMIW && (p[1] & 0xC0) == 0x80 &&
				mem_load32(p + 6) == 0) {
			off_callerrecords = mem_load32(p + 2) + 8;
			return true;
		}
		NEXT_INSN(p, "offset to vote caller record vector");
	}
#else
#warning TODO(linux): this too
#endif
	return false;
}

#ifdef _WIN32
static inline bool find_TheDirector(void *GameShutdown) {
	// in 2045, literally the first instruction of this function is loading
	// TheDirector into ECX. although, do the usual search in case moves a bit.
	const uchar *insns = (const uchar *)GameShutdown;
	for (const uchar *p = insns; p - insns < 24;) {
		if (p[0] == X86_MOVRMW && p[1] == X86_MODRM(0, 1, 5)) {
			void **indirect = mem_loadptr(p + 2);
			director = *indirect;
			return true;
		}
		NEXT_INSN(p, "load of TheDirector");
	}
	return false;
}
#endif

static inline bool find_UnfreezeTeam(void *GameFrame) { // note: L4D1 only
	// CServerGameDLL::GameFrame() loads TheDirector into ECX and then calls
	// Director::Update()
	const uchar *insns = (const uchar *)GameFrame, *p = insns;
	while (p - insns < 192) {
		if (p[0] == X86_MOVRMW && p[1] == X86_MODRM(0, 1, 5) &&
				mem_loadptr(mem_loadptr(p + 2)) == director &&
				p[6] == X86_CALL) {
			p += 11 + mem_loadoffset(p + 7);
			insns = p;
			goto ok;
		}
		NEXT_INSN(p, "Director::Update call");
	}
	return false;
ok: // Director::Update calls UnfreezeTeam after the first jmp instruction
	while (p - insns < 96) {
		// jz XXX; mov ecx, <reg>; call Director::UnfreezeTeam
		if (p[0] == X86_JZ && p[2] == X86_MOVRMW && (p[3] & 0xF8) == 0xC8 &&
				p[4] == X86_CALL) {
			p += 9 + mem_loadoffset(p + 5);
			orig_UnfreezeTeam = (UnfreezeTeam_func)p;
			return true;
		}
		NEXT_INSN(p, "Director::UnfreezeTeam call");
	}
	return false;
 }

INIT {
	struct con_cmd *cmd_listissues = con_findcmd("listissues");
	if (!cmd_listissues) {
		errmsg_errorx("couldn't find \"listissues\" command");
		return false;
	}
	con_cmdcbv1 listissues_cb = con_getcmdcbv1(cmd_listissues);
	const uchar *nextinsns = find_votecontroller(listissues_cb);
	if (!nextinsns) {
		errmsg_errorx("couldn't find vote controller variable");
		return false;
	}
	if (!find_voteissues(nextinsns)) {
		errmsg_errorx("couldn't find vote issues list offset\n");
		return false;
	}
	void **vtable;
#ifdef _WIN32
	void *GameShutdown = (*(void ***)srvdll)[vtidx_GameShutdown];
	if (!find_TheDirector(GameShutdown)) {
		errmsg_errorx("couldn't find TheDirector variable");
		return false;
	}
#else
#warning TODO(linux): should be able to just dlsym(server, "TheDirector")
	return false;
#endif
#ifdef _WIN32 // L4D1 has no Linux build, no need to check whether L4D2
	if (GAMETYPE_MATCHES(L4D2)) {
#endif
		vtable = mem_loadptr(director);
		if (!os_mprot(vtable + vtidx_OnGameplayStart, sizeof(*vtable),
				PAGE_READWRITE)) {
			errmsg_errorsys("couldn't make virtual table writable");
			return false;
		}
		orig_OnGameplayStart = (OnGameplayStart_func)hook_vtable(vtable,
				vtidx_OnGameplayStart, (void *)&hook_OnGameplayStart);
#ifdef _WIN32 // L4D1 has no Linux build!
	}
	else /* L4D1 */ {
		void *GameFrame = (*(void ***)srvdll)[vtidx_GameFrame];
		if (!find_UnfreezeTeam(GameFrame)) {
			errmsg_errorx("couldn't find UnfreezeTeam function");
			return false;
		}
		orig_UnfreezeTeam = (UnfreezeTeam_func)hook_inline(
				(void *)orig_UnfreezeTeam, (void *)&hook_UnfreezeTeam);
	}
#endif
	// Only try cooldown stuff for L4D2, since L4D1 always had unlimited votes.
	if (GAMETYPE_MATCHES(L4D2)) {
		// g_voteController is invalid if not running a server so get the
		// vtable by inspecting the ent factory code instead
		const struct CEntityFactory *factory = ent_getfactory("vote_controller");
		if (!factory) {
			errmsg_errorx("couldn't find vote controller entity factory");
			goto nocd;
		}
		vtable = ent_findvtable(factory, "CVoteController");
		if (!vtable) {
			errmsg_errorx("couldn't find CVoteController vtable");
			goto nocd;
		}
		if (!find_votecallers(vtable[vtidx_Spawn])) {
			errmsg_errorx("couldn't find vote callers list offset");
nocd:		errmsg_note("resetting a first map will not clear vote cooldowns");
		}
	}
	con_reg(sst_l4d_quickreset);
	sst_l4d_quickreset_fastfwd->base.flags &= ~CON_HIDDEN;
	return true;
}

END {
	if (GAMETYPE_MATCHES(L4D2)) {
		unhook_vtable(mem_loadptr(director), vtidx_OnGameplayStart,
				(void *)orig_OnGameplayStart);
	}
	else {
		unhook_inline((void *)orig_UnfreezeTeam);
	}
}

// vi: sw=4 ts=4 noet tw=80 cc=80
