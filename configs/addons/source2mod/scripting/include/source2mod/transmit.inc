/**
 * Source2Mod SourcePawn API: transmit (ISource2ModTransmit).
 *
 * Which entities each player receives. An entity is hooked once, then shown
 * or hidden per viewer on channels (it shows only when no channel hides it);
 * a player is hidden through their controller and the pawn goes with it.
 * The CheckTransmit hook hands over every viewer's transmit set per tick.
 */

#if defined _source2mod_transmit_included
 #endinput
#endif
#define _source2mod_transmit_included

#include <source2mod/types>
#include <source2mod/schema>

/** One viewer's transmit set; valid inside the CheckTransmit hook. */
methodmap ISource2ModTransmitInfo
{
	public native CPlayerSlot GetPlayerSlot();
	public native bool IsFullUpdate();
	public native bool IsTransmitting(int entityIndex);

	/**
	 * Keeps the entity from this viewer -- and tells the client to keep a
	 * record of it, so it can come back later without a crash.
	 */
	public native void BlockTransmit(int entityIndex);
	public native void AllowTransmit(int entityIndex);
}

/** The viewers of one CheckTransmit call. */
methodmap ISource2ModTransmitInfoList
{
	public native ISource2ModTransmitInfo Get(int index);
}

/**
 * Every viewer's transmit set, once per tick: the viewers, and the entity
 * indices being considered. Runs every tick -- keep it cheap.
 */
typedef Source2ModCheckTransmitHook = function void (ISource2ModTransmitInfoList infos, int infoCount, const int[] entityIndices, int entityCount);

methodmap Source2ModTransmit
{
	/** Hooks an entity for per-viewer visibility; false when it already is. The plugin owns the hook. */
	public static native bool HookEntity(CEntityInstance entity, bool defaultVisible = true);
	public static native void UnhookEntity(CEntityInstance entity);
	public static native bool IsEntityHooked(CEntityInstance entity);

	public static native bool SetVisible(CEntityInstance entity, CPlayerSlot viewer, bool visible, int channel = 0);
	public static native bool SetVisibleToAll(CEntityInstance entity, bool visible, int channel = 0);
	public static native bool IsVisible(CEntityInstance entity, CPlayerSlot viewer);
	public static native bool IsHiddenOnChannel(CEntityInstance entity, CPlayerSlot viewer, int channel);

	public static native bool SetBlockAll(CEntityInstance entity, bool block);
	public static native bool GetBlockAll(CEntityInstance entity);

	/** An entity that follows its owner's visibility. */
	public static native bool SetOwner(CEntityInstance entity, CEntityInstance owner);
	public static native CEntityInstance GetOwner(CEntityInstance entity);

	/** Forgets every visibility setting for a viewer (they left). */
	public static native void ResetViewer(CPlayerSlot viewer);

	/** @return The id UnhookCheckTransmit takes */
	public static native int HookCheckTransmit(Source2ModCheckTransmitHook handler);
	public static native bool UnhookCheckTransmit(int id);
}
