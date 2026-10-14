/**
 * Source2Mod SourcePawn API: ConVars (ISource2ModConVars, CConVar, CConVarRef).
 *
 * A CConVar is the plugin's own typed ConVar; CConVarRef finds any existing
 * one. Both are ConVar handles, so ConVar's properties and methods
 * (IntValue, FloatValue, SetString, ...) read and write them.
 */

#if defined _source2mod_convars_included
 #endinput
#endif
#define _source2mod_convars_included

#include <source2mod/types>

#if !defined FCVAR_NONE
 #define FCVAR_NONE 0
#endif

/**
 * Every ConVar change, for all of them: C++'s HookConVarChange handler.
 *
 * @param convar        The ConVar that changed
 * @param slot          Split-screen slot of the change (-1 for all)
 * @param newValue      The value being set
 * @param oldValue      The value it replaces
 */
typedef Source2ModConVarChangeHandler = function void (ConVar convar, int slot, const char[] newValue, const char[] oldValue);

methodmap CConVar < ConVar
{
	/** CConVar<int>: name, flags, help, default, optional bounds. */
	public static native CConVar CreateInt(const char[] name, int flags, const char[] help, int defaultValue,
	                                       bool hasMin = false, int min = 0, bool hasMax = false, int max = 0);

	/** CConVar<float> */
	public static native CConVar CreateFloat(const char[] name, int flags, const char[] help, float defaultValue,
	                                         bool hasMin = false, float min = 0.0, bool hasMax = false, float max = 0.0);

	/** CConVar<bool> */
	public static native CConVar CreateBool(const char[] name, int flags, const char[] help, bool defaultValue);

	/** CConVar<CUtlString> */
	public static native CConVar CreateString(const char[] name, int flags, const char[] help, const char[] defaultValue);
}

methodmap CConVarRef < ConVar
{
	/** An existing ConVar by name; null when there is none. */
	public static native CConVarRef Find(const char[] name);

	public native bool IsValidRef();

	/** Whether the ConVar's data is there (it is registered, not just referenced). */
	public native bool IsConVarDataAvailable();
}

methodmap Source2ModConVars
{
	/**
	 * Listens to every ConVar change.
	 *
	 * @return The id UnhookConVarChange takes
	 */
	public static native int HookConVarChange(Source2ModConVarChangeHandler handler);

	public static native bool UnhookConVarChange(int id);
}
