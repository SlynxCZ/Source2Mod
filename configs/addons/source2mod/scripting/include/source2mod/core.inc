/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * SourceMod (C)2004-2008 AlliedModders LLC.  All rights reserved.
 * =============================================================================
 *
 * This file is part of the SourceMod/SourcePawn SDK.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, AlliedModders LLC gives you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */

/**
 * Source2Mod SourcePawn API: the language core every script needs -- the
 * plugin's version record and myinfo, Handle, NULL_VECTOR / NULL_STRING,
 * optional natives. Taken from SourceMod's core.inc, version.inc and
 * handles.inc.
 */

#if defined _source2mod_core_included
 #endinput
#endif
#define _source2mod_core_included

#tryinclude <version_auto>

#if !defined _auto_version_included
 #define SOURCE2MOD_V_TAG         "manual"
 #define SOURCE2MOD_V_REV         0
 #define SOURCE2MOD_V_CSET        "0"
 #define SOURCE2MOD_V_MAJOR       1               /**< Source2Mod Major version */
 #define SOURCE2MOD_V_MINOR       0               /**< Source2Mod Minor version */
 #define SOURCE2MOD_V_RELEASE     0               /**< Source2Mod Release version */

 #define SOURCE2MOD_VERSION       "1.0.0-manual"  /**< Source2Mod version string (major.minor.release-tag) */
#endif

/** If this gets changed, you need to update Core's check. */
#define SOURCE2MOD_PLUGINAPI_VERSION     7

struct PlVers
{
	public int version;
	public const char[] filevers;
	public const char[] date;
	public const char[] time;
};

/**
 * What a forward's function answers (GlobalForward / PrivateForward with
 * ET_Hook / ET_Event / ET_Single). Game hooks answer Source2ModAction.
 */
enum Action
{
	Plugin_Continue = 0,    /**< Continue with the original action */
	Plugin_Changed = 1,     /**< Inputs or outputs have been overridden with new values */
	Plugin_Handled = 3,     /**< Handle the action at the end (don't call it) */
	Plugin_Stop = 4         /**< Immediately stop the hook chain and handle the original */
};

/**
 * Specifies identity types.
 */
enum Identity
{
	Identity_Core = 0,
	Identity_Extension = 1,
	Identity_Plugin = 2
};

public PlVers __version =
{
	version = SOURCE2MOD_PLUGINAPI_VERSION,
	filevers = SOURCE2MOD_VERSION,
	date = __DATE__,
	time = __TIME__
};

/**
 * Plugin status values.
 */
enum PluginStatus
{
	Plugin_Running=0,       /**< Plugin is running */
	/* All states below are "temporarily" unexecutable */
	Plugin_Paused,          /**< Plugin is loaded but paused */
	Plugin_Error,           /**< Plugin is loaded but errored/locked */
	/* All states below do not have all natives */
	Plugin_Loaded,          /**< Plugin has passed loading and can be finalized */
	Plugin_Failed,          /**< Plugin has a fatal failure */
	Plugin_Created,         /**< Plugin is created but not initialized */
	Plugin_Uncompiled,      /**< Plugin is not yet compiled by the JIT */
	Plugin_BadLoad,         /**< Plugin failed to load */
	Plugin_Evicted          /**< Plugin was unloaded due to an error */
};

/**
 * Plugin information properties. Plugins can declare a global variable with
 * their info. Example,
 *
 *   public Plugin myinfo = {
 *   	name = "Admin Help",
 *   	author = "AlliedModders LLC",
 *   	description = "Display command information",
 *   	version = "1.0",
 *   	url = "http://www.sourcemod.net/"
 *   };
 *
 * Source2Mod will display this information when a user inspects plugins in the
 * console.
 */
enum PluginInfo
{
	PlInfo_Name,            /**< Plugin name */
	PlInfo_Author,          /**< Plugin author */
	PlInfo_Description,     /**< Plugin description */
	PlInfo_Version,         /**< Plugin version */
	PlInfo_URL              /**< Plugin URL */
};

/**
 * Plugin public information.
 */
struct Plugin
{
	public const char[] name;           /**< Plugin Name */
	public const char[] description;    /**< Plugin Description */
	public const char[] author;         /**< Plugin Author */
	public const char[] version;        /**< Plugin Version */
	public const char[] url;            /**< Plugin URL */
};

/**
 * Defines how an extension must expose itself for autoloading.
 */
struct Extension
{
	public const char[] name;   /**< Short name */
	public const char[] file;   /**< Default file name */
	public bool autoload;       /**< Whether or not to auto-load */
	public bool required;       /**< Whether or not to require */
};

/**
 * Defines how a plugin must expose itself for native requiring.
 */
struct SharedPlugin
{
	public const char[] name;   /**< Short name */
	public const char[] file;   /**< File name */
	public bool required;       /**< Whether or not to require */
};

public float NULL_VECTOR[3];        /**< Pass this into certain functions to act as a C++ NULL */
public const char NULL_STRING[1];   /**< pass this into certain functions to act as a C++ NULL */

/**
 * Check if the given vector is the NULL_VECTOR.
 *
 * @param vec     The vector to test.
 * @return        True if NULL_VECTOR, false otherwise.
 */
native bool IsNullVector(const float vec[3]);

/**
 * Check if the given string is the NULL_STRING.
 *
 * @param str     The string to test.
 * @return        True if NULL_STRING, false otherwise.
 */
native bool IsNullString(const char[] str);

/**
 * Horrible compatibility shim.
 */
public Extension __ext_core =
{
	name = "Core",
	file = "core",
	autoload = 0,
	required = 0,
};

native int VerifyCoreVersion();

/**
 * Sets a native as optional, such that if it is unloaded, removed,
 * or otherwise non-existent, the plugin will still work.  Calling
 * removed natives results in a run-time error. What a library's include
 * calls from its __pl_<library>_SetNTVOptional().
 *
 * @param name          Native name.
 */
native void MarkNativeAsOptional(const char[] name);

public void __ext_core_SetNTVOptional()
{
	VerifyCoreVersion();
}

#define AUTOLOAD_EXTENSIONS
#define REQUIRE_EXTENSIONS
#define REQUIRE_PLUGIN

/**
 * Preset Handle values.
 */
enum Handle // Tag disables introducing "Handle" as a symbol.
{
	INVALID_HANDLE = 0
};

/**
 * A reference to an object the core keeps for the plugin (an ArrayList, a
 * timer, a SQL connection, ...). `delete handle` closes it.
 */
methodmap Handle __nullable__ {
    public native ~Handle();

    /**
     * Closes a Handle.  If the handle has multiple copies open,
     * it is not destroyed unless all copies are closed.
     *
     * @note Closing a Handle has a different meaning for each Handle type.  Make
     *       sure you read the documentation on whatever provided the Handle.
     *
     * @error           Invalid handles will cause a run time error.
     */
    public native void Close();

    /**
     * Clones a Handle.  When passing handles in between plugins, caching handles
     * can result in accidental invalidation when one plugin releases the Handle, or is its owner
     * is unloaded from memory.  To prevent this, the Handle may be "cloned" with a new owner.
     *
     * @param plugin    Optional Handle to another plugin to mark as the new owner.
     *                  If no owner is passed, the owner becomes the calling plugin.
     * @return          Handle on success, INVALID_HANDLE if not cloneable.
     * @error           Invalid handles will cause a run time error.
     */
    public native Handle Clone(Handle plugin=INVALID_HANDLE);
};
