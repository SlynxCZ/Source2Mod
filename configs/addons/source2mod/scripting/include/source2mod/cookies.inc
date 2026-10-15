/**
 * Source2Mod SourcePawn API: client preferences (ISource2ModCookies).
 *
 * Per-player settings kept in a database under the player's SteamID and
 * loaded when the player is authorized. A cookie is shared by name between
 * plugins; here it is the id Register() / Find() return (0 is none).
 */

#if defined _source2mod_cookies_included
 #endinput
#endif
#define _source2mod_cookies_included

#include <source2mod/types>

/**
 * Who may see and change a cookie. SourceMod's CookieAccess (clientprefs.inc,
 * (C)2004-2014 AlliedModders LLC, GPLv3 with the AlliedModders exception).
 */
enum CookieAccess
{
	CookieAccess_Public,            /**< Visible and Changeable by users */
	CookieAccess_Protected,         /**< Read only to users */
	CookieAccess_Private            /**< Completely hidden cookie */
};

/** A player's cookies were loaded (after OnClientAuthorized). */
forward void Source2Mod_OnClientCookiesCached(CPlayerSlot slot);

methodmap Source2ModCookies
{
	/** Whether the cookie database is open; without it values last only as long as the player stays. */
	public static native bool IsConnected();

	/** Registers a cookie, or finds it if registered. @return The cookie, 0 for an empty or too long (> 30) name. */
	public static native int Register(const char[] name, const char[] description, CookieAccess access);

	/** @return The cookie, 0 when there is none by that name. */
	public static native int Find(const char[] name);

	/** How many cookies are registered; ids run from 1 to that. */
	public static native int Count();

	public static native bool GetName(int cookie, char[] buffer, int maxlen);
	public static native bool GetDescription(int cookie, char[] buffer, int maxlen);
	public static native CookieAccess GetAccess(int cookie);

	/** Whether the player's values were loaded. */
	public static native bool AreCached(CPlayerSlot slot);

	/** @return false (and "") when the player never had a value or it is not loaded yet. */
	public static native bool Get(CPlayerSlot slot, int cookie, char[] buffer, int maxlen);

	/** Sets and saves the value (at most 100 bytes). */
	public static native bool Set(CPlayerSlot slot, int cookie, const char[] value);

	/** When the value was last set, Unix time; 0 when never. */
	public static native int GetTime(CPlayerSlot slot, int cookie);

	/** Sets the value of a player who need not be connected: a SteamID64, "STEAM_1:Y:Z" or "[U:1:N]". */
	public static native bool SetBySteamID(const char[] steamId, int cookie, const char[] value);
}
