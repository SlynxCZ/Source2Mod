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
#include <clients>

// SteamID forms: SourceMod's AuthIdType (AuthId_Engine, AuthId_Steam2,
// AuthId_Steam3, AuthId_SteamID64), the same values as ESource2ModAuthId.

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
	public static native int GetAuthId(CPlayerSlot slot, AuthIdType form, char[] buffer, int maxlen, bool validated = true);

	/** Average round trip in seconds (0 for a bot or no client). */
	public static native float GetLatency(CPlayerSlot slot);

	/** Seconds connected (0 for a bot or no client). */
	public static native float GetConnectionTime(CPlayerSlot slot);

	/** A cvar the client sent (cl_language, ...). @return Bytes written */
	public static native int GetClientConVar(CPlayerSlot slot, const char[] name, char[] buffer, int maxlen);

	/** Disconnects a client with a message they are shown. */
	public static native void Kick(CPlayerSlot slot, const char[] format, any ...);

	/** Clients on the server, bots included. */
	public static native int GetClientCount(bool inGameOnly = true);
}
