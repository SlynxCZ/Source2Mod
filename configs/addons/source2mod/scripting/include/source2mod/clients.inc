/**
 * Source2Mod SourcePawn API: player helpers (ISource2ModClients).
 *
 * What the controller methodmaps already do (name, IP, SteamID64, user id,
 * console commands, team changes, Disconnect) stays there.
 */

#if defined _source2mod_clients_included
 #endinput
#endif
#define _source2mod_clients_included

#include <source2mod/types>

/** SteamID forms (ESource2ModAuthId). */
enum Source2ModAuthIdForm
{
	AuthIdForm_Engine = 0,    /**< as the engine shows it (Steam2) */
	AuthIdForm_Steam2,        /**< STEAM_1:0:12345 */
	AuthIdForm_Steam3,        /**< [U:1:24690] */
	AuthIdForm_SteamID64      /**< 76561197960290418 */
}

methodmap Source2ModClients
{
	/** A slot has a client (connecting or in game). */
	public static native bool IsConnected(CPlayerSlot slot);

	/** A slot's client is fully in game. */
	public static native bool IsInGame(CPlayerSlot slot);
	public static native bool IsFakeClient(CPlayerSlot slot);

	/** Steam has confirmed the client's SteamID. */
	public static native bool IsAuthorized(CPlayerSlot slot);

	/**
	 * A client's SteamID as text; empty for a bot, no client, or (validated)
	 * one Steam has not confirmed yet.
	 * @return Bytes written
	 */
	public static native int GetAuthId(CPlayerSlot slot, Source2ModAuthIdForm form, char[] buffer, int maxlen, bool validated = true);

	/** Average round trip in seconds (0 for a bot or no client). */
	public static native float GetLatency(CPlayerSlot slot);

	/** Seconds connected (0 for a bot or no client). */
	public static native float GetConnectionTime(CPlayerSlot slot);

	/** A cvar the client sent (cl_language, ...). @return Bytes written */
	public static native int GetClientConVar(CPlayerSlot slot, const char[] name, char[] buffer, int maxlen);

	/** Disconnects a client. CS2 shows them the kick reason's own text; the message is what the server logs. */
	public static native void Kick(CPlayerSlot slot, const char[] format, any ...);

	/** Clients on the server, bots included. */
	public static native int GetClientCount(bool inGameOnly = true);

	/**
	 * Any SteamID form (STEAM_X:Y:Z, [U:1:N], SteamID64) in another form.
	 * @return false when steamId is none of them
	 */
	public static native bool ConvertSteamId(const char[] steamId, Source2ModAuthIdForm form, char[] buffer, int maxlen);

	/** A connected client's user id (what #<userid> targets); -1 when there is no client. */
	public static native int GetUserId(CPlayerSlot slot);

	/** The slot of a connected client by user id; CPlayerSlot(-1) when there is none. */
	public static native CPlayerSlot FromUserId(int userid);

	/** The slot of a connected client by SteamID (any form); CPlayerSlot(-1) when there is none. */
	public static native CPlayerSlot FromSteamId(const char[] steamId);
}
