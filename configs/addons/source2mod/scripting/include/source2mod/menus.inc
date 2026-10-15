/**
 * Source2Mod SourcePawn API: menus (ISource2ModMenus, CenterHtmlMenu).
 *
 * A CenterHtmlMenu is the script's until deleted; deleting it closes it for
 * everyone who has it open (also where it is a submenu). Players pick with the
 * slot keys: the numbered options of the page, 7 Back (previous page, else the
 * parent menu), 8 Next, 9 Exit -- so 1-6 options a page. Options can be fixed
 * or worked out per player (text, disabled, visible), open a submenu, or be
 * plain text lines that take no number.
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

/** Why a menu went away for a player (MenuEndReason). */
enum CenterHtmlMenuEnd
{
	MenuEnd_Selected = 0,    /**< an option closed it (MenuOption_Close, PostSelect_Close) */
	MenuEnd_Exit,            /**< the player pressed Exit */
	MenuEnd_Replaced,        /**< another menu was opened over it */
	MenuEnd_Closed,          /**< Close / CloseActiveMenu / the menu was deleted */
	MenuEnd_Disconnected,    /**< the player left */
	MenuEnd_Timeout          /**< the Timeout ran out */
};

// Option flags (SetOptionFlags / GetOptionFlags).
#define MenuOption_Disabled (1 << 0)    /**< greyed out, cannot be picked */
#define MenuOption_Hidden   (1 << 1)    /**< not shown */
#define MenuOption_Close    (1 << 2)    /**< close the menu after it is picked */
#define MenuOption_Stay     (1 << 3)    /**< keep the page after it is picked */

/** option is the index the Add* call returned. */
typedef CenterHtmlMenuHandler = function void (CCSPlayerController player, CenterHtmlMenu menu, int option, any data);

/**
 * A state worked out for the player it is shown to (disabled, visible);
 * asked each time the menu is drawn (every frame while open).
 */
typedef CenterHtmlMenuCheck = function bool (CCSPlayerController player, CenterHtmlMenu menu, int option, any data);

/** Text worked out for the player it is shown to; option is -1 for the title. */
typedef CenterHtmlMenuText = function void (CCSPlayerController player, CenterHtmlMenu menu, int option, char[] buffer, int maxlen, any data);

/** The menu (opened as the root) went away for a player. */
typedef CenterHtmlMenuEndCallback = function void (CCSPlayerController player, CenterHtmlMenu menu, CenterHtmlMenuEnd reason);

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
	 * for 5 seconds. evaluator: as SetOptionDisabledEvaluator, with the same data.
	 */
	public native int AddMenuOptionWithCooldown(const char[] text, CenterHtmlMenuHandler handler, any data = 0, bool disabled = false, bool close = true,
	                                            CenterHtmlMenuCheck evaluator = INVALID_FUNCTION);

	/** An option that opens submenu; its Back button comes back here. @return Its index */
	public native int AddSubmenu(const char[] text, CenterHtmlMenu submenu, bool disabled = false);

	/** A line of text that cannot be picked and takes no number. @return Its index */
	public native int AddText(const char[] text);

	/**
	 * An option whose text, and optionally whether it is disabled / shows, is
	 * worked out for each player it is shown to. @return Its index
	 */
	public native int AddDynamicOption(CenterHtmlMenuText text, CenterHtmlMenuHandler handler = INVALID_FUNCTION, any data = 0,
	                                   CenterHtmlMenuCheck disabled = INVALID_FUNCTION, CenterHtmlMenuCheck visible = INVALID_FUNCTION);

	public native void ClearOptions();
	public native int OptionCount();
	public native void SetOptionText(int option, const char[] text);
	public native int GetOptionText(int option, char[] buffer, int maxlen);
	public native void SetOptionDisabled(int option, bool disabled);
	public native bool IsOptionDisabled(int option);

	/** Decides the option's disabled state from now on; INVALID_FUNCTION removes it. */
	public native void SetOptionDisabledEvaluator(int option, CenterHtmlMenuCheck evaluator, any data = 0);

	/** MenuOption_* flags. */
	public native void SetOptionFlags(int option, int flags);
	public native int GetOptionFlags(int option);

	/** A title worked out per player; INVALID_FUNCTION goes back to the fixed one. */
	public native void SetDynamicTitle(CenterHtmlMenuText title, any data = 0);

	/** Called when the menu, opened as the root, goes away for a player; INVALID_FUNCTION removes it. */
	public native void OnEnd(CenterHtmlMenuEndCallback callback);

	/** Options a page, 1-6 (default 5). */
	property int ItemsPerPage
	{
		public native get();
		public native set(int count);
	}

	/** Seconds until it closes by itself (MenuEnd_Timeout); 0 = never. */
	property int Timeout
	{
		public native get();
		public native set(int seconds);
	}

	/** Closes it for one player, or for everyone (also where it is a submenu) with NULL_ENTITY. */
	public native void Close(CCSPlayerController player = view_as<CCSPlayerController>(NULL_ENTITY));

	/** Whether the player has this menu open, as the root or a submenu on their path. */
	public native bool IsOpenFor(CCSPlayerController player);

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

	/** The menu the player is looking at (the submenu they are in) when it is one of this script's; null otherwise. */
	public static native CenterHtmlMenu GetActiveMenu(CCSPlayerController player);

	public static native void CloseActiveMenu(CCSPlayerController player);

	/** Draws the player's open menu now (it is redrawn every frame anyway). */
	public static native void RedrawActiveMenu(CCSPlayerController player);

	/** Takes the player's open menu back to its first page (and out of submenus). */
	public static native void ResetActiveMenu(CCSPlayerController player);

	/** Feeds a key (1..9) to the player's open menu. */
	public static native void OnKeyPress(CCSPlayerController player, int key);
}
