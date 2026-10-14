/**
 * Source2Mod SourcePawn API: custom HUD (ISource2ModCustomHud). Button
 * clicks on a CCSCustomHudLayout entity.
 */

#if defined _source2mod_customhud_included
 #endinput
#endif
#define _source2mod_customhud_included

#include <source2mod/schema>

typedef Source2ModCustomHudClickHandler = function void (CCSPlayerController player, CCSCustomHudLayout layout, const char[] buttonId);

methodmap Source2ModCustomHud
{
	/** @return The id UnhookCustomHudClick takes */
	public static native int HookCustomHudClick(CCSCustomHudLayout layout, Source2ModCustomHudClickHandler handler);
	public static native bool UnhookCustomHudClick(int id);

	/** Every hook of this script on the layout. */
	public static native void UnhookCustomHudClickLayout(CCSCustomHudLayout layout);
}
