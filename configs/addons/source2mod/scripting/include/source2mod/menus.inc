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

/* ---- menu builder (ISource2ModMenuBuilder) ------------------------------------
 *
 * Menus built item by item: pages, submenus with a Back button, items whose
 * text or state is worked out per player when shown. Shown as the center
 * HTML menu, picked with the slot keys. A menu can be open for many players
 * at once, each on their own page and submenu path. delete closes it for
 * everyone (submenus are separate handles).
 */

// Item flags.
#define MenuItem_Disabled   (1 << 0)    /**< greyed out, cannot be picked */
#define MenuItem_Hidden     (1 << 1)    /**< not shown */
#define MenuItem_Close      (1 << 2)    /**< close the menu after it is picked */
#define MenuItem_Stay       (1 << 3)    /**< keep the page after it is picked */

/** Why a menu went away for a player (ESource2ModMenuEnd). */
enum Source2ModMenuEnd
{
	MenuClosed_Selected = 0,
	MenuClosed_Exit,
	MenuClosed_Replaced,
	MenuClosed_Closed,
	MenuClosed_Disconnected,
	MenuClosed_Timeout
};

/** An item was picked: the menu it is in, its index there and its data. */
typedef Source2ModMenuSelect = function void (CCSPlayerController player, Source2ModMenu menu, int item, any data);

/** Text worked out when the menu is shown to a player. */
typedef Source2ModMenuText = function void (CCSPlayerController player, char[] buffer, int maxlen, any data);

/** A state worked out when the menu is shown (enabled, visible). */
typedef Source2ModMenuCheck = function bool (CCSPlayerController player, any data);

/** The menu went away for a player. */
typedef Source2ModMenuEndCallback = function void (CCSPlayerController player, Source2ModMenu menu, Source2ModMenuEnd reason);

methodmap Source2ModMenu < Handle
{
	public native Source2ModMenu(const char[] title);

	public native void SetTitle(const char[] title);

	/** A title worked out per player (replaces the fixed one). */
	public native void SetDynamicTitle(Source2ModMenuText title, any data = 0);

	/** @return The item's index */
	public native int AddItem(const char[] text, Source2ModMenuSelect onSelect = INVALID_FUNCTION, any data = 0, int flags = 0);

	/** An item whose text (and whether it can be picked / shows) is worked out each time. @return Its index */
	public native int AddDynamicItem(Source2ModMenuText text, Source2ModMenuSelect onSelect = INVALID_FUNCTION, any data = 0, int flags = 0,
	                                 Source2ModMenuCheck enabled = INVALID_FUNCTION, Source2ModMenuCheck visible = INVALID_FUNCTION);

	/** An item that opens submenu; its Back button comes back here. @return Its index */
	public native int AddSubmenu(const char[] text, Source2ModMenu submenu, int flags = 0);

	/** A line that cannot be picked and takes no number. @return Its index */
	public native int AddText(const char[] text);

	property int ItemCount
	{
		public native get();
	}

	public native void SetItemText(int item, const char[] text);
	public native void SetItemFlags(int item, int flags);
	public native int GetItemFlags(int item);
	public native void RemoveItem(int item);
	public native void ClearItems();

	/** 1-7, default 5. */
	property int ItemsPerPage
	{
		public native get();
		public native set(int count);
	}

	property bool ExitButton
	{
		public native get();
		public native set(bool value);
	}

	/** After an item without MenuItem_Close/_Stay is picked (default PostSelect_Reset). */
	property PostSelectAction PostSelect
	{
		public native get();
		public native set(PostSelectAction action);
	}

	/** Closes the menu after this many seconds (0 = never). */
	property int Timeout
	{
		public native get();
		public native set(int seconds);
	}

	/** HTML colors; "" keeps one as it is. */
	public native void SetColors(const char[] title, const char[] enabled, const char[] disabled, const char[] navigation);

	public native void OnEnd(Source2ModMenuEndCallback callback);

	/** Shows the menu (page 1), replacing what the player has open. */
	public native void Display(CCSPlayerController player);

	/** Closes it for one player, or for everyone with NULL_ENTITY. */
	public native void Close(CCSPlayerController player = view_as<CCSPlayerController>(NULL_ENTITY));

	/** Whether the player has this menu (or one of its submenus) open. */
	public native bool IsOpenFor(CCSPlayerController player);

	/** The builder menu the player has open (the submenu they are in), if it is this script's; null otherwise. */
	public static native Source2ModMenu GetOpenMenu(CCSPlayerController player);
}
