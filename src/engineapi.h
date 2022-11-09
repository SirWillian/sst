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

/*
 * This header was named by mlugg. Please direct all filename-related
 * bikeshedding to <mlugg@mlugg.co.uk>.
 */

#ifndef INC_ENGINEAPI_H
#define INC_ENGINEAPI_H

#include "intdefs.h"
#include "vcall.h"

/*
 * Here, we define a bunch of random data types as well as interfaces that don't
 * have abstractions elsewhere but nonetheless need to be used in a few
 * different places.
 */

/* Access to game and engine factories obtained on plugin load */
typedef void *(*ifacefactory)(const char *name, int *ret);
extern ifacefactory factory_client, factory_server, factory_engine,
	   factory_inputsystem;

// various engine types {{{

struct VEngineClient {
	void **vtable;
	/* opaque fields */
};

struct VEngineServer {
	void **vtable;
	/* opaque fields */
};

struct CUtlMemory {
	void *mem;
	int alloccnt, growsz;
};
struct CUtlVector {
	struct CUtlMemory m;
	int sz;
	void *mem_again_for_some_reason;
};

struct edict {
	// CBaseEdict
	int stateflags;
	int netserial;
	void *ent_networkable;
	void *ent_unknown;
	// edict_t
	// NOTE! *REMOVED* in l4d-based branches. don't iterate over edict pointers!
	float freetime;
};

/* an RGBA colour, passed to con_colourmsg */
struct rgba_colour {
	union {
		struct { u8 r, g, b, a; };
		u32 val;
		uchar bytes[4];
	};
};

struct vec3f { float x, y, z; };
struct CMoveData {
	bool firstrun : 1, gamecodemoved : 1;
	ulong playerhandle;
	int impulse;
	struct vec3f viewangles, absviewangles;
	int buttons, oldbuttons;
	float mv_forward, mv_side, mv_up;
	float maxspeed, clmaxspeed;
	struct vec3f vel, angles, oldangles;
	float out_stepheight;
	struct vec3f out_wishvel, out_jumpvel;
	struct vec3f constraint_centre;
	float constraint_radius, constraint_width, constraint_speedfactor;
	struct vec3f origin;
};

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


#define SENDPROP_INT 0
#define SENDPROP_FLOAT 1
#define SENDPROP_VEC 2
#define SENDPROP_VECXY 3
#define SENDPROP_STR 4
#define SENDPROP_ARRAY 5
#define SENDPROP_DTABLE 6
#define SENDPROP_INT64 7

// these have to be opaque because, naturally, they're unstable between
// branches - access stuff using gamedata offsets as usual
struct RecvProp;
struct SendProp;

// these two things seem to be stable enough for our purposes
struct SendTable {
	struct SendProp *props;
	int nprops;
	char *tablename;
	void *precalc;
	bool inited : 1;
	bool waswritten : 1;
	/* "has props encoded against current tick count" ??? */
	bool haspropsenccurtickcnt : 1;
};
struct ServerClass {
	char *name;
	struct SendTable *table;
	struct ServerClass *next;
	int id;
	int instbaselineidx;
};

/// }}}

extern struct VEngineClient *engclient;
extern struct VEngineServer *engserver;
extern void *globalvars;

/*
 * Called on plugin init to attempt to initialise various core interfaces.
 * This includes console/cvar initialisation and populating gametype and
 * gamedata values.
 *
 * Returns true if there is enough stuff in place for the plugin to function -
 * there may still be stuff missing. Returns false if there's no way the plugin
 * can possibly work, e.g. if there's no cvar interface.
 */
bool engineapi_init(int pluginver);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80 fdm=marker
