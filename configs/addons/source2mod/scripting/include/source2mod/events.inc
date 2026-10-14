/**
 * Source2Mod SourcePawn API: game events (ISource2ModEvents, IGameEvent,
 * IGameEventManager2).
 *
 * IGameEvent is the core Event handle with the CS2 player accessors on
 * top; the Event getters and setters work on it.
 */

#if defined _source2mod_events_included
 #endinput
#endif
#define _source2mod_events_included

#include <events>
#include <source2mod/types>

methodmap IGameEvent < Event
{
	/** A player field (e.g. "userid") as the player's slot. */
	public native CPlayerSlot GetPlayerSlot(const char[] key);

	/** A player field as the player's controller; NULL_ENTITY when there is none. */
	public native CCSPlayerController GetPlayerController(const char[] key);

	/** A player field as the player's pawn. */
	public native CCSPlayerPawn GetPlayerPawn(const char[] key);

	/** Sets a player field to a slot. */
	public native void SetPlayer(const char[] key, CPlayerSlot slot);
}

methodmap IGameEventManager2
{
	/**
	 * A new event, the plugin's until it is fired with FireEvent() or freed.
	 *
	 * @param force     Create it even when nobody listens
	 * @return          The event, null when it does not exist
	 */
	public static native IGameEvent CreateEvent(const char[] name, bool force = false);

	/** To the server and every client, through every hook. Frees the event. */
	public static native bool FireEvent(IGameEvent event, bool dontBroadcast = false);

	/** Frees an event that was not given to FireEvent() (FireEventToClient leaves it yours). */
	public static native void FreeEvent(IGameEvent event);
}

/**
 * A game event hook.
 *
 * Pre (post = false): fields may be changed, dontBroadcast keeps it from the
 * clients (answer Action_Override then), Action_Supersede cancels it.
 */
typedef Source2ModGameEventHandler = function Source2ModAction (IGameEvent event, bool post, bool &dontBroadcast);

methodmap Source2ModEvents
{
	/** @return The id UnhookGameEvent takes, 0 when the event does not exist */
	public static native int HookGameEvent(const char[] name, Source2ModGameEventHandler handler, bool post);

	public static native bool UnhookGameEvent(int id);
}
