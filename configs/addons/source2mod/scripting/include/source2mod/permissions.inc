/**
 * Source2Mod SourcePawn API: permissions (ISource2ModPermissions).
 *
 * Dotted permissions ("admin.kick"), groups with immunity, per-player
 * grants. SteamIDs are 64-bit, so they are decimal text here. What a plugin
 * grants or creates is tagged with it (the C++ owner argument is the
 * calling script).
 */

#if defined _source2mod_permissions_included
 #endinput
#endif
#define _source2mod_permissions_included

#include <source2mod/types>
#include <source2mod/commands>

/**
 * Called instead of the command when a caller lacks its permission.
 */
typedef Source2ModAccessDeniedHandler = function void (Source2ModCommandContext context, Source2ModCommandArgs args, const char[] permission);

methodmap Source2ModPermissions
{
	public static native bool HasPermission(const char[] steamId, const char[] permission);
	public static native bool PlayerHasPermission(CPlayerSlot slot, const char[] permission);

	/** The permission configured for `name` (a command), or defaultPermission. */
	public static native bool CheckAccess(CPlayerSlot slot, const char[] name, const char[] defaultPermission);

	public static native bool InGroup(const char[] steamId, const char[] group);
	public static native int GetImmunity(const char[] steamId);
	public static native bool CanTarget(CPlayerSlot caller, CPlayerSlot target);
	public static native void GetPlayerSteamID(CPlayerSlot slot, char[] buffer, int maxlen);
	public static native bool IsPlayerAuthorized(CPlayerSlot slot);

	public static native void GrantPermission(const char[] steamId, const char[] permission);
	public static native void RevokePermission(const char[] steamId, const char[] permission);
	public static native void AddToGroup(const char[] steamId, const char[] group);
	public static native void RemoveFromGroup(const char[] steamId, const char[] group);
	public static native void SetImmunity(const char[] steamId, int immunity);
	public static native void ClearPlayer(const char[] steamId);

	public static native void CreateGroup(const char[] group, int immunity);
	public static native void DeleteGroup(const char[] group);
	public static native void AddGroupPermission(const char[] group, const char[] permission);
	public static native void RemoveGroupPermission(const char[] group, const char[] permission);
	public static native void AddGroupParent(const char[] group, const char[] parent);
	public static native void RemoveGroupParent(const char[] group, const char[] parent);

	/** @return The id ClearAccessDeniedHandler takes */
	public static native int SetAccessDeniedHandler(Source2ModAccessDeniedHandler handler);
	public static native bool ClearAccessDeniedHandler(int id);
}
