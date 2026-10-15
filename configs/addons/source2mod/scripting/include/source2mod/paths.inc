/**
 * Source2Mod SourcePawn API: paths (ISource2ModPaths). Absolute paths,
 * without a trailing separator.
 */

#if defined _source2mod_paths_included
 #endinput
#endif
#define _source2mod_paths_included

/** A buffer big enough for any path the core hands out (SourceMod's PLATFORM_MAX_PATH). */
#define PLATFORM_MAX_PATH   256

methodmap Source2ModPaths
{
	/** The game folder (.../game/csgo). */
	public static native void GameDirectory(char[] buffer, int maxlen);

	/** addons/source2mod */
	public static native void RootDirectory(char[] buffer, int maxlen);

	/** addons/source2mod/configs */
	public static native void ConfigsDirectory(char[] buffer, int maxlen);

	/** addons/source2mod/gamedata */
	public static native void GamedataDirectory(char[] buffer, int maxlen);

	/** addons/source2mod/plugins */
	public static native void PluginsDirectory(char[] buffer, int maxlen);

	/** addons/source2mod/shared */
	public static native void SharedDirectory(char[] buffer, int maxlen);

	/** The calling plugin's own config folder (made when missing). */
	public static native void PluginConfigDirectory(char[] buffer, int maxlen);
}
