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

	/** Sets a player field to a slot (and its _pawn key). */
	public native void SetPlayer(const char[] key, CPlayerSlot slot);

	/** The event's id in the event manager's list (LookupEventId). */
	public native int GetID();

	/** Whether the event is sent reliably. */
	public native bool IsReliable();

	/** Whether the event is never networked (server only). */
	public native bool IsLocal();

	/** Whether a field is unset (C++ IsEmpty). */
	public native bool IsEmpty(const char[] key);

	/** Whether the event has a field of this name. */
	public native bool HasKey(const char[] key);

	/** A uint64 field (xuid, steamid) as decimal text. */
	public native void GetUint64(const char[] key, char[] buffer, int maxlen, const char[] defValue = "0");

	/** Sets a uint64 field from decimal text. */
	public native void SetUint64(const char[] key, const char[] value);

	/**
	 * An entity handle field as a reference (CEntityInstance.FromRefEHandle
	 * turns it into the entity); `defValue` when unset (-1 = an empty handle).
	 */
	public native int GetEHandle(const char[] key, int defValue = -1);

	/** An entity field as the entity; mostly for _pawn keys. NULL_ENTITY when there is none. */
	public native CEntityInstance GetEntity(const char[] key, CEntityInstance fallback = view_as<CEntityInstance>(-1));

	/** An entity field as an index; `defValue` when unset. */
	public native int GetEntityIndex(const char[] key, int defValue = -1);

	/** The _pawn entity of a player field, as a reference (see GetEHandle). */
	public native int GetPawnEHandle(const char[] key);

	/** The _pawn entity of a player field, as an index. */
	public native int GetPawnEntityIndex(const char[] key);

	/** Sets an entity field (NULL_ENTITY clears it). */
	public native void SetEntity(const char[] key, CEntityInstance entity);

	/** Sets an entity field from a reference (GetRefEHandle). */
	public native void SetEntityHandle(const char[] key, int ref);

	/** Sets a player field from the player's pawn (and its _pawn key): SetPlayer(key, pawn) in C++. */
	public native void SetPlayerPawn(const char[] key, CBasePlayerPawn pawn);

	/** Sets controllerKey to the pawn's controller and pawnKey to the pawn. */
	public native void SetPlayerRaw(const char[] controllerKey, const char[] pawnKey, CBasePlayerPawn pawn);

	/** GetDataKeys(): how many fields the event holds. */
	public native int GetDataKeyCount();

	/** GetDataKeys(): the name of field `index` (0 .. GetDataKeyCount() - 1). */
	public native int GetDataKeyName(int index, char[] buffer, int maxlen);
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

	/** To the server's own listeners only, not to the clients. Frees the event. */
	public static native bool FireEventClientSide(IGameEvent event);

	/**
	 * A copy of an event -- a hooked one included -- the plugin's like one
	 * from CreateEvent(): fire it or free it.
	 */
	public static native IGameEvent DuplicateEvent(IGameEvent event);

	/** Loads event descriptions (a .gameevents / resource file); returns how many were loaded. */
	public static native int LoadEventsFromFile(const char[] filename, bool searchAll = false);

	/** The event's id, -1 when there is no such event. */
	public static native int LookupEventId(const char[] name);

	/** Whether an event of this name exists. */
	public static native bool HasEventDescriptor(const char[] name);

	/** The event with its fields, as the engine prints it. */
	public static native void PrintEventToString(IGameEvent event, char[] buffer, int maxlen);

	/**
	 * Writes the event into a CMsgSource1LegacyGameEvent message
	 * (Source2ModNetworkMessages, message GE_Source1LegacyGameEvent), to be
	 * sent with the network messages API.
	 */
	public static native bool SerializeEvent(IGameEvent event, NetMessage msg);

	/** An event made from a CMsgSource1LegacyGameEvent message; the plugin's, like one from CreateEvent(). */
	public static native IGameEvent UnserializeEvent(NetMessage msg);
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
