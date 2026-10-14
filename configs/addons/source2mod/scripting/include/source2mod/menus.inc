/**
 * Source2Mod SourcePawn API: menus (ISource2ModMenus, CenterHtmlMenu).
 *
 * A CenterHtmlMenu is the script's until deleted; deleting it closes it for
 * everyone who has it open. Players pick options with the slot keys.
 */

#if defined _source2mod_menus_included
 #endinput
#endif
#define _source2mod_menus_included

#include <source2mod/schema>

enum PostSelectAction
{
	PostSelect_Close = 0,
	PostSelect_Reset,
	PostSelect_Nothing
};

enum CenterHtmlMenuColor
{
	MenuColor_Title = 0,    // "yellow"
	MenuColor_Enabled,      // "green"
	MenuColor_Disabled,     // "grey"
	MenuColor_PrevPage,     // "yellow"
	MenuColor_NextPage,     // "yellow"
	MenuColor_Close         // "red"
};

/** option is the index AddMenuOption returned. */
typedef CenterHtmlMenuHandler = function void (CCSPlayerController player, CenterHtmlMenu menu, int option, any data);

methodmap CenterHtmlMenu < Handle
{
	public native CenterHtmlMenu(const char[] title);

	public native void SetTitle(const char[] title);
	public native int GetTitle(char[] buffer, int maxlen);

	/** @return The option's index */
	public native int AddMenuOption(const char[] text, CenterHtmlMenuHandler handler, any data = 0, bool disabled = false);

	/**
	 * IBaseMenu::AddMenuOptionWithCooldown with the default cooldown handlers:
	 * close = after a pick, close the menu and show the option text centered
	 * for 5 seconds.
	 */
	public native int AddMenuOptionWithCooldown(const char[] text, CenterHtmlMenuHandler handler, any data = 0, bool disabled = false, bool close = true);

	public native void ClearOptions();
	public native int OptionCount();
	public native void SetOptionText(int option, const char[] text);
	public native int GetOptionText(int option, char[] buffer, int maxlen);
	public native void SetOptionDisabled(int option, bool disabled);
	public native bool IsOptionDisabled(int option);

	property PostSelectAction PostSelect
	{
		public native get();
		public native set(PostSelectAction action);
	}

	property bool ExitButton
	{
		public native get();
		public native set(bool exitButton);
	}

	/** An HTML color name or "#rrggbb". */
	public native void SetColor(CenterHtmlMenuColor which, const char[] color);
	public native int GetColor(CenterHtmlMenuColor which, char[] buffer, int maxlen);
}

methodmap Source2ModMenus
{
	public static native void OpenCenterHtmlMenu(CCSPlayerController player, CenterHtmlMenu menu);

	/** Whether the player has any menu open (of any plugin). */
	public static native bool HasActiveMenu(CCSPlayerController player);

	/** The player's open menu when it is one of this script's; null otherwise. */
	public static native CenterHtmlMenu GetActiveMenu(CCSPlayerController player);

	public static native void CloseActiveMenu(CCSPlayerController player);

	/** Feeds a key (1..9) to the player's open menu. */
	public static native void OnKeyPress(CCSPlayerController player, int key);
}
