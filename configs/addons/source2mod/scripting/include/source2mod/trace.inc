/**
 * Source2Mod SourcePawn API: traces (ISource2ModTrace).
 *
 * Contents masks are 64-bit: two cells, like player masks (mask[0] bits
 * 0..31, mask[1] bits 32..63). CONTENTS_*_BIT are bit numbers; the
 * Mask_* stocks build the common MASK_* sets.
 */

#if defined _source2mod_trace_included
 #endinput
#endif
#define _source2mod_trace_included

#include <source2mod/types>
#include <source2mod/schema>

// Bit numbers of the contents layers (const.h LAYER_INDEX_CONTENTS_*).
#define CONTENTS_SOLID_BIT                      0
#define CONTENTS_HITBOX_BIT                     1
#define CONTENTS_TRIGGER_BIT                    2
#define CONTENTS_SKY_BIT                        3
#define CONTENTS_PLAYER_CLIP_BIT                4
#define CONTENTS_NPC_CLIP_BIT                   5
#define CONTENTS_BLOCK_LOS_BIT                  6
#define CONTENTS_BLOCK_LIGHT_BIT                7
#define CONTENTS_LADDER_BIT                     8
#define CONTENTS_PICKUP_BIT                     9
#define CONTENTS_BLOCK_SOUND_BIT                10
#define CONTENTS_NODRAW_BIT                     11
#define CONTENTS_WINDOW_BIT                     12
#define CONTENTS_PASS_BULLETS_BIT               13
#define CONTENTS_WORLD_GEOMETRY_BIT             14
#define CONTENTS_WATER_BIT                      15
#define CONTENTS_SLIME_BIT                      16
#define CONTENTS_TOUCH_ALL_BIT                  17
#define CONTENTS_PLAYER_BIT                     18
#define CONTENTS_NPC_BIT                        19
#define CONTENTS_DEBRIS_BIT                     20
#define CONTENTS_PHYSICS_PROP_BIT               21
#define CONTENTS_NAV_IGNORE_BIT                 22
#define CONTENTS_NAV_LOCAL_IGNORE_BIT           23
#define CONTENTS_POST_PROCESSING_VOLUME_BIT     24
#define CONTENTS_VEHICLE_CLIP_BIT               25
#define CONTENTS_CARRIED_OBJECT_BIT             26
#define CONTENTS_PUSHAWAY_BIT                   27
#define CONTENTS_SERVER_ENTITY_ON_CLIENT_BIT    28
#define CONTENTS_CARRIED_WEAPON_BIT             29
#define CONTENTS_STATIC_LEVEL_BIT               30
#define CONTENTS_CSGO_TEAM1_BIT                 31
#define CONTENTS_CSGO_TEAM2_BIT                 32
#define CONTENTS_CSGO_GRENADE_CLIP_BIT          33
#define CONTENTS_CSGO_DRONE_CLIP_BIT            34
#define CONTENTS_CSGO_MOVEABLE_BIT              35
#define CONTENTS_CSGO_OPAQUE_BIT                36
#define CONTENTS_CSGO_MONSTER_BIT               37
#define CONTENTS_CSGO_UNUSED_LAYER_BIT          38
#define CONTENTS_CSGO_THROWN_GRENADE_BIT        39

stock void Mask_Clear(int mask[2])
{
	mask[0] = 0;
	mask[1] = 0;
}

stock void Mask_Add(int mask[2], int bit)
{
	mask[bit >> 5] |= (1 << (bit & 31));
}

stock bool Mask_Has(const int mask[2], int bit)
{
	return (mask[bit >> 5] & (1 << (bit & 31))) != 0;
}

/** MASK_SHOT: solid, player, NPC, window, debris, hitbox. */
stock void Mask_Shot(int mask[2])
{
	Mask_Clear(mask);
	Mask_Add(mask, CONTENTS_SOLID_BIT);
	Mask_Add(mask, CONTENTS_PLAYER_BIT);
	Mask_Add(mask, CONTENTS_NPC_BIT);
	Mask_Add(mask, CONTENTS_WINDOW_BIT);
	Mask_Add(mask, CONTENTS_DEBRIS_BIT);
	Mask_Add(mask, CONTENTS_HITBOX_BIT);
}

/** MASK_SOLID: solid, window, player, NPC, pass-bullets. */
stock void Mask_Solid(int mask[2])
{
	Mask_Clear(mask);
	Mask_Add(mask, CONTENTS_SOLID_BIT);
	Mask_Add(mask, CONTENTS_WINDOW_BIT);
	Mask_Add(mask, CONTENTS_PLAYER_BIT);
	Mask_Add(mask, CONTENTS_NPC_BIT);
	Mask_Add(mask, CONTENTS_PASS_BULLETS_BIT);
}

/** MASK_PLAYERSOLID: MASK_SOLID and player clip. */
stock void Mask_PlayerSolid(int mask[2])
{
	Mask_Solid(mask);
	Mask_Add(mask, CONTENTS_PLAYER_CLIP_BIT);
}

/** MASK_SHOT_HULL: MASK_SHOT without hitbox, with pass-bullets. */
stock void Mask_ShotHull(int mask[2])
{
	Mask_Clear(mask);
	Mask_Add(mask, CONTENTS_SOLID_BIT);
	Mask_Add(mask, CONTENTS_PLAYER_BIT);
	Mask_Add(mask, CONTENTS_NPC_BIT);
	Mask_Add(mask, CONTENTS_WINDOW_BIT);
	Mask_Add(mask, CONTENTS_DEBRIS_BIT);
	Mask_Add(mask, CONTENTS_PASS_BULLETS_BIT);
}

/** MASK_WATER: water, slime. */
stock void Mask_Water(int mask[2])
{
	Mask_Clear(mask);
	Mask_Add(mask, CONTENTS_WATER_BIT);
	Mask_Add(mask, CONTENTS_SLIME_BIT);
}

/**
 * TraceOptions: what the trace is (InteractsAs), what it hits
 * (InteractsWith, MASK_SHOT by default) and what it skips.
 */
methodmap TraceOptions < Handle
{
	public native TraceOptions();
	public native void SetInteractsAs(const int mask[2]);
	public native void SetInteractsWith(const int mask[2]);
	public native void SetInteractsExclude(const int mask[2]);
	public native void GetInteractsAs(int mask[2]);
	public native void GetInteractsWith(int mask[2]);
	public native void GetInteractsExclude(int mask[2]);
}

/** A trace's result: the script's until deleted. */
methodmap TraceResult < Handle
{
	public native void StartPos(float pos[3]);
	public native void EndPos(float pos[3]);
	public native void HitPoint(float pos[3]);
	public native void Normal(float normal[3]);
	public native float Fraction();
	public native float HitOffset();
	public native bool DidHit();
	public native bool IsAllSolid();
	public native bool HasExactHit();

	/** NULL_ENTITY when nothing (or the world without an entity) was hit. */
	public native CEntityInstance HitEntity();

	public native int TriangleIndex();
	public native int HitboxBoneIndex();
	public native int Contents();
	public native int RayType();
}

methodmap Source2ModTrace
{
	/** Along the angles from start, to the end of the world. */
	public static native TraceResult TraceShape(const float start[3], const float angles[3], CBaseEntity ignore = NULL_ENTITY, TraceOptions options = null);

	/** A line from start to end. */
	public static native TraceResult TraceEndShape(const float start[3], const float end[3], CBaseEntity ignore = NULL_ENTITY, TraceOptions options = null);

	/** A box (mins..maxs) swept from start to end. */
	public static native TraceResult TraceHullShape(const float start[3], const float end[3], const float mins[3], const float maxs[3],
	                                                CBaseEntity ignore = NULL_ENTITY, TraceOptions options = null);

	/** The contents at a point, filtered by mask. */
	public static native void PointContents(const float pos[3], const int mask[2], int contents[2]);

	public static native void GetEntityWorldSpaceAABB(CBaseEntity entity, float mins[3], float maxs[3]);
}
