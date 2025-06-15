#include "accessor.h"
#include "con_.h"
#include "engineapi.h"
#include "ent.h"
#include "feature.h"
#include "gamedata.h"
#include "math.h"
#include "trace.h"

FEATURE("oob entity detection")
REQUIRE(ent)
REQUIRE(trace)
REQUIRE_GAMEDATA(off_name)
REQUIRE_GAMEDATA(off_classname)
REQUIRE_GAMEDATA(off_globalname)
REQUIRE_GAMEDATA(off_entpos)
REQUIRE_GAMEDATA(off_collision)
REQUIRE_GAMEDATA(off_solidtype)
REQUIRE_GAMEDATA(off_solidflags)

DEF_ACCESSORS(void, char *, name)
DEF_ACCESSORS(void, char *, classname)
DEF_ACCESSORS(void, char *, globalname)
DEF_PTR_ACCESSOR(void, struct vec3f, entpos)

// TODO(opt): figure out if these offsets are constant between engines
struct CCollisionProperty;
DEF_PTR_ACCESSOR(void/*CBaseEntity*/, struct CCollisionProperty, collision)
DEF_ACCESSORS(void, struct vec3f, mins)
DEF_ACCESSORS(void, struct vec3f, maxs)
DEF_ACCESSORS(void, u8, solidtype)
DEF_ACCESSORS(void, u16, solidflags)

// ICollideable
DECL_VFUNC_DYN(struct CCollisionProperty,
	const struct vec3f *, GetCollisionOrigin)
DECL_VFUNC_DYN(struct CCollisionProperty,
	const float(*)[3][4], CollisionToWorldTransform)

// class CTraceFilterWorldOnly : public ITraceFilter
static bool VCALLCONV ShouldHitEntity(void *this, void *ent, int contents) {
	return false;
}
static int VCALLCONV GetTraceType(void *this) { return 1; /*TRACE_WORLD_ONLY*/ }
static struct { void **vt; } filter = {
	(void*[]){(void *)&ShouldHitEntity, (void *)&GetTraceType}
};

// bool CCollisionProperty::IsBoundsDefinedInEntitySpace() from the SDK
// return (( m_usSolidFlags & FSOLID_FORCE_WORLD_ALIGNED ) == 0 ) &&
// 		( m_nSolidType != SOLID_BBOX ) && ( m_nSolidType != SOLID_NONE );
// SOLID_NONE was already checked before calling this
static inline bool boundsinentspace(int solidtype, int solidflags) {
	return (solidflags & 64) == 0 && solidtype != 2;
}

static struct vec3f get_collworldcenter(void *ent, int type, int flags) {
	struct vec3f mins = get_mins(ent), maxs = get_maxs(ent);
	struct vec3f out = {
		.x = (mins.x + maxs.x) / 2,
		.y = (mins.y + maxs.y) / 2,
		.z = (mins.z + maxs.z) / 2,
	};
	// The engine code adds something like this to the if statement below:
	// || GetCollisionAngles(col) == (struct vec3f){0}
	// That virtual call will ultimately get m_angAbsRotation from the base
	// entity in an attempt to avoid a matrix transform. I'll bet that it's
	// better to take the matrix transform than always doing an extra virtual
	// call.
	// XXX: all these virtual calls will recompute the entity's absolute
	// position/rotation/ent-to-world space matrix if it has a dirty
	// ABSTRANSFORM flag. The engine *seems* to always access these members
	// through their appropriate getters that also trigger this recalculation,
	// so us doing it shouldn't cause anything evil.
	struct CCollisionProperty *coll = getptr_collision(ent);
	if (!boundsinentspace(type, flags)) {
		const struct vec3f *coll_origin = GetCollisionOrigin(coll);
		out.x += coll_origin->x;
		out.y += coll_origin->y;
		out.z += coll_origin->z;
	}
	else {
		const float (*matrix)[3][4] = CollisionToWorldTransform(coll);
		out = vec3f_transform(out, *matrix);
	}
	return out;
}

// MASK_PLAYERSOLID_BRUSHONLY, combination of CONTENTS_* flags:
// SOLID|MOVEABLE|WINDOW|PLAYERCLIP|GRATE
#define TRACEMASK (0x1 | 0x4000 | 0x2 | 0x10000 | 0x8)

DEF_FEAT_CCMD_HERE(sst_print_oob_ents, "Prints entities that are oob", CON_CHEAT) {
	if (!ent_get(0)) return;
	for (int i = 2; i < (1 << 11); i++) {
		struct edict *ed = ent_getedict(i);
		if (!ed || !ed->ent_unknown) continue;
		void *ent = ed->ent_unknown;
		int solidflags = get_solidflags(ent), solidtype = get_solidtype(ent);
		// ignore not-solid collision props
		// SOLID_NONE || & FSOLID_NOT_SOLID
		if (solidtype == 0 || (solidflags & 4) != 0)
			continue;
		struct vec3f pos = get_collworldcenter(ent, solidtype, solidflags);
		if (trace_ispointoob(&pos)) {
			// end position doesn't matter. we just want to test the start pos
			struct vec3f end = {pos.x + 1, pos.y + 1, pos.z + 1};
			struct CGameTrace tr = trace_line(pos, end, TRACEMASK, &filter);
			if (!tr.base.startsolid)
				con_msg("%s %s (%f %f %f)\n", get_name(ent), get_classname(ent),
						pos.x, pos.y, pos.z);
		}
	}
}

INIT {
	return FEAT_OK;
}

END {

}
