/**
 * Source2Mod SourcePawn API: cs_script (ISource2ModScripts). Named point
 * scripts loaded from an asset, source text or a file; non-persistent ones
 * go away on map change. What a plugin loads goes with it.
 */

#if defined _source2mod_scripts_included
 #endinput
#endif
#define _source2mod_scripts_included

#include <source2mod/schema>

/**
 * A message a script posted on a channel:
 * Instance.ServerCommand("source2mod_script <channel> <payload>"). On a
 * workshop map only a lone command gets through (no ";" or newline in it),
 * so encode the payload (encodeURIComponent).
 */
typedef Source2ModScriptMessageHandler = function void (const char[] channel, const char[] payload);

methodmap Source2ModScripts
{
	/** Whether scripts can be compiled from source on this server. */
	public static native bool CanRunSource();

	/** NULL_ENTITY when it failed. */
	public static native CCSPointScriptEntity LoadAsset(const char[] name, const char[] asset, bool persistent = false);
	public static native CCSPointScriptEntity RunSource(const char[] name, const char[] virtualPath, const char[] source, bool persistent = false);
	public static native CCSPointScriptEntity RunFile(const char[] name, const char[] filePath, bool persistent = false);
	public static native CCSPointScriptEntity Reload(const char[] name);
	public static native void Remove(const char[] name);
	public static native CCSPointScriptEntity Find(const char[] name);
	public static native bool FireInput(const char[] name, const char[] input);

	/** @return The id UnhookScriptMessage takes */
	public static native int HookScriptMessage(const char[] channel, Source2ModScriptMessageHandler handler);
	public static native bool UnhookScriptMessage(int id);
}
