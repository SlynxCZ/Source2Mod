/**
 * Source2Mod SourcePawn API: game hooks (ISource2ModGameHooks).
 *
 * Game functions the core hooks from its own gamedata. A handler gets the
 * call's context -- valid inside the handler -- and post, and answers an
 * action; for a function with a return value, `result` is what the caller
 * gets when the handler answers Action_Override or Action_Supersede.
 * CallOriginal() (pre) runs the game's function now, past every hook, and
 * returns its value; answer Action_Supersede after it, or it runs twice.
 */

#if defined _source2mod_gamehooks_included
 #endinput
#endif
#define _source2mod_gamehooks_included

#include <source2mod/types>
#include <source2mod/schema>

/** The damage being dealt (CTakeDamageInfo); valid inside the hook. */
methodmap CTakeDamageInfo
{
	property float m_flDamage
	{
		public native get();
		public native set(float value);
	}
	property float m_flOriginalDamage
	{
		public native get();
	}
	property float m_flTotalledDamage
	{
		public native get();
	}
	property int m_bitsDamageType
	{
		public native get();
		public native set(int value);
	}
	property int m_iDamageCustom
	{
		public native get();
		public native set(int value);
	}
	property int m_iHitGroupId
	{
		public native get();
	}
	property CBaseEntity m_hInflictor
	{
		public native get();
	}
	property CBaseEntity m_hAttacker
	{
		public native get();
	}
	property CBaseEntity m_hAbility
	{
		public native get();
	}
	public native void m_vecDamageForce(float vec[3]);
	public native void Set_m_vecDamageForce(const float vec[3]);
	public native void m_vecDamagePosition(float vec[3]);
	public native void m_vecDamageDirection(float vec[3]);
}

/** CBaseEntity::TakeDamage(info, result) -> int64 */
methodmap TakeDamageContext
{
	property CBaseEntity entity
	{
		public native get();
	}
	property CTakeDamageInfo info
	{
		public native get();
	}
	/** Runs the game's TakeDamage now (pre only); returns its result. */
	public native int CallOriginal();
}

/** CCSPlayerPawnBase::PostThink() */
methodmap PostThinkContext
{
	property CCSPlayerPawnBase pawn
	{
		public native get();
	}
	public native void CallOriginal();
}

typedef Source2ModTakeDamageHandler = function Source2ModAction (TakeDamageContext ctx, bool post, int &result);
typedef Source2ModPostThinkHandler = function Source2ModAction (PostThinkContext ctx, bool post);

methodmap Source2ModGameHooks
{
	/** @return The id UnhookTakeDamage takes */
	public static native int HookTakeDamage(Source2ModTakeDamageHandler handler, bool post);
	public static native bool UnhookTakeDamage(int id);

	/** Every pawn, every tick: do as little as possible. */
	public static native int HookPostThink(Source2ModPostThinkHandler handler, bool post);
	public static native bool UnhookPostThink(int id);
}
