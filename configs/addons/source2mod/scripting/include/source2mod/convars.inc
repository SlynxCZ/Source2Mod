/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * SourceMod (C)2004-2014 AlliedModders LLC.  All rights reserved.
 * =============================================================================
 *
 * The ConVar methodmap, ConVarBounds and ConVarChanged below are SourceMod's
 * (convars.inc). This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public License, version
 * 3.0, as published by the Free Software Foundation. As a special exception,
 * AlliedModders LLC gives you permission to link the code of this program (as
 * well as its derivative works) to "Half-Life 2," the "Source Engine," the
 * "SourcePawn JIT," and any Game MODs that run on software by the Valve
 * Corporation. See <http://www.sourcemod.net/license.php>.
 */

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

#include <source2mod/core>
#include <source2mod/types>

// ConVar / command flags, CS2's values (s2sdk tier0/convar.h). The 64-bit
// ones above bit 31 go through Source2ModConVars.GetFlags / SetFlags.
#define FCVAR_NONE                  0
#define FCVAR_LINKED_CONCOMMAND     (1<<0)
#define FCVAR_DEVELOPMENTONLY       (1<<1)
#define FCVAR_GAMEDLL               (1<<2)
#define FCVAR_CLIENTDLL             (1<<3)
#define FCVAR_HIDDEN                (1<<4)
#define FCVAR_PROTECTED             (1<<5)
#define FCVAR_SPONLY                (1<<6)
#define FCVAR_ARCHIVE               (1<<7)
#define FCVAR_NOTIFY                (1<<8)
#define FCVAR_USERINFO              (1<<9)
#define FCVAR_REFERENCE             (1<<10)
#define FCVAR_UNLOGGED              (1<<11)
#define FCVAR_INITIAL_SETVALUE      (1<<12)
#define FCVAR_REPLICATED            (1<<13)
#define FCVAR_CHEAT                 (1<<14)
#define FCVAR_PER_USER              (1<<15)
#define FCVAR_DEMO                  (1<<16)
#define FCVAR_DONTRECORD            (1<<17)
#define FCVAR_RELEASE               (1<<19)
#define FCVAR_MENUBAR_ITEM          (1<<20)
#define FCVAR_COMMANDLINE_ENFORCED  (1<<21)
#define FCVAR_NOT_CONNECTED         (1<<22)
#define FCVAR_SERVER_CAN_EXECUTE    (1<<24)
#define FCVAR_CLIENT_CAN_EXECUTE    (1<<25)
#define FCVAR_SERVER_CANNOT_QUERY   (1<<26)
#define FCVAR_CLIENTCMD_CAN_EXECUTE (1<<28)
#define FCVAR_EXECUTE_PER_TICK      (1<<29)
#define FCVAR_SNAPSHOT_IGNORED      (1<<30)

/**
 * Every ConVar change, for all of them: C++'s HookConVarChange handler.
 *
 * @param convar        The ConVar that changed
 * @param slot          Split-screen slot of the change (-1 for all)
 * @param newValue      The value being set
 * @param oldValue      The value it replaces
 */
typedef Source2ModConVarChangeHandler = function void (ConVar convar, int slot, const char[] newValue, const char[] oldValue);

// ConVar, ConVarBounds and ConVarChanged come from SourceMod's convars.inc
// (C)2004-2014 AlliedModders LLC, GPLv3 with the AlliedModders exception.

/**
 * Console variable bound values used with Get/SetConVarBounds()
 */
enum ConVarBounds
{
	ConVarBound_Upper = 0,
	ConVarBound_Lower
};

/**
 * Called when a console variable's value is changed.
 * 
 * @param convar        Handle to the convar that was changed.
 * @param oldValue      String containing the value of the convar before it was changed.
 * @param newValue      String containing the new value of the convar.
 */
typedef ConVarChanged = function void (ConVar convar, const char[] oldValue, const char[] newValue);

methodmap ConVar < Handle
{
	// Retrieves or sets a boolean value for the convar.
	property bool BoolValue {
		public native get();
		public native set(bool b);
	}

	// Retrieves or sets an integer value for the convar.
	property int IntValue {
		public native get();
		public native set(int value);
	}

	// Retrieves or sets a float value for the convar.
	property float FloatValue {
		public native get();
		public native set(float value);
	}

	// Gets or sets the flag bits (FCVAR_*) on the convar.
	property int Flags {
		public native get();
		public native set(int flags);
	}

	// Sets the boolean value of a console variable.
	//
	// Note: The replicate and notify params are only relevant for the
	// original, Dark Messiah, and Episode 1 engines. Newer engines
	// automatically do these things when the convar value is changed.
	//
	// @param value     New boolean value.
	// @param replicate If set to true, the new convar value will be set on all clients.
	//                  This will only work if the convar has the FCVAR_REPLICATED flag
	//                  and actually exists on clients.
	// @param notify    If set to true, clients will be notified that the convar has changed.
	//                  This will only work if the convar has the FCVAR_NOTIFY flag.
	public native void SetBool(bool value, bool replicate=false, bool notify=false);

	// Sets the integer value of a console variable.
	//
	// Note: The replicate and notify params are only relevant for the
	// original, Dark Messiah, and Episode 1 engines. Newer engines
	// automatically do these things when the convar value is changed.
	//
	// @param value     New integer value.
	// @param replicate If set to true, the new convar value will be set on all clients.
	//                  This will only work if the convar has the FCVAR_REPLICATED flag
	//                  and actually exists on clients.
	// @param notify    If set to true, clients will be notified that the convar has changed.
	//                  This will only work if the convar has the FCVAR_NOTIFY flag.
	public native void SetInt(int value, bool replicate=false, bool notify=false);

	// Sets the floating point value of a console variable.
	//
	// Note: The replicate and notify params are only relevant for the
	// original, Dark Messiah, and Episode 1 engines. Newer engines
	// automatically do these things when the convar value is changed.
	//
	// @param value     New floating point value.
	// @param replicate If set to true, the new convar value will be set on all clients.
	//                  This will only work if the convar has the FCVAR_REPLICATED flag
	//                  and actually exists on clients.
	// @param notify    If set to true, clients will be notified that the convar has changed.
	//                  This will only work if the convar has the FCVAR_NOTIFY flag.
	public native void SetFloat(float value, bool replicate=false, bool notify=false);

	// Retrieves the string value of a console variable.
	//
	// @param value      Buffer to store the value of the convar.
	// @param maxlength  Maximum length of string buffer.
	public native void GetString(char[] value, int maxlength);

	// Sets the string value of a console variable.
	//
	// Note: The replicate and notify params are only relevant for the
	// original, Dark Messiah, and Episode 1 engines. Newer engines
	// automatically do these things when the convar value is changed.
	//
	// @param value      New string value.
	// @param replicate  If set to true, the new convar value will be set on all clients.
	//                   This will only work if the convar has the FCVAR_REPLICATED flag
	//                   and actually exists on clients.
	// @param notify     If set to true, clients will be notified that the convar has changed.
	//                   This will only work if the convar has the FCVAR_NOTIFY flag.
	public native void SetString(const char[] value, bool replicate=false, bool notify=false);

	// Resets the console variable to its default value.
	//
	// Note: The replicate and notify params are only relevant for the
	// original, Dark Messiah, and Episode 1 engines. Newer engines
	// automatically do these things when the convar value is changed.
	//
	// @param replicate  If set to true, the new convar value will be set on all clients.
	//                   This will only work if the convar has the FCVAR_REPLICATED flag
	//                   and actually exists on clients.
	// @param notify     If set to true, clients will be notified that the convar has changed.
	//                   This will only work if the convar has the FCVAR_NOTIFY flag.
	public native void RestoreDefault(bool replicate=false, bool notify=false);

	// Retrieves the default string value of a console variable.
	//
	// @param value      Buffer to store the default value of the convar.
	// @param maxlength  Maximum length of string buffer.
	// @return           Number of bytes written to the buffer (UTF-8 safe).
	public native int GetDefault(char[] value, int maxlength);

	// Retrieves the specified bound of a console variable.
	//
	// @param type       Type of bound to retrieve, ConVarBound_Lower or ConVarBound_Upper.
	// @param value      By-reference cell to store the specified floating point bound value.
	// @return           True if the convar has the specified bound set, false otherwise.
	public native bool GetBounds(ConVarBounds type, float &value);

	// Retrieves the name of a console variable.
	//
	// @param name       Buffer to store the name of the convar.
	// @param maxlength  Maximum length of string buffer.
	public native void GetName(char[] name, int maxlength);

	// Retrieves the description of a console variable.
	//
	// @param buffer     Buffer to store the description of the convar.
	// @param maxlength  Maximum length of string buffer.
	public native void GetDescription(char[] buffer, int maxlength);

	// Creates a hook for when a console variable's value is changed.
	//
	// @param callback  An OnConVarChanged function pointer.
	public native void AddChangeHook(ConVarChanged callback);

	// Removes a hook for when a console variable's value is changed.
	//
	// @param callback  An OnConVarChanged function pointer.
	// @error           No active hook on convar.
	public native void RemoveChangeHook(ConVarChanged callback);
}

/** A ConVar's value type (C++ EConVarType). */
enum EConVarType
{
	EConVarType_Invalid = -1,
	EConVarType_Bool,
	EConVarType_Int16,
	EConVarType_UInt16,
	EConVarType_Int32,
	EConVarType_UInt32,
	EConVarType_Int64,
	EConVarType_UInt64,
	EConVarType_Float32,
	EConVarType_Float64,
	EConVarType_String,
	EConVarType_Color,
	EConVarType_Vector2,
	EConVarType_Vector3,
	EConVarType_Vector4,
	EConVarType_Qangle,
	EConVarType_VectorWS
};

methodmap CConVar < ConVar
{
	/**
	 * ISource2ModConVars::CreateConVar: a ConVar of any type, the values as
	 * text -- "1 2 3" for a vector or angle, "255 0 0 255" for a color, a
	 * decimal for a 64-bit integer. Bounds are ignored for strings. An
	 * existing ConVar of that name is returned as it is.
	 */
	public static native CConVar Create(const char[] name, EConVarType type, int flags, const char[] help, const char[] defaultValue,
	                                    bool hasMin = false, const char[] min = "", bool hasMax = false, const char[] max = "");

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

	/**
	 * The ISource2ModConVars accessors ConVar's own properties do not cover;
	 * the C++ access index is the ConVar handle. Name, help text, string,
	 * bool, int and float values are ConVar's GetName, GetDescription,
	 * GetString, BoolValue, IntValue and FloatValue.
	 */
	public static native EConVarType GetType(ConVar convar);

	/** All 64 flag bits as two cells, flags[0] the low half (ConVar.Flags has the low 32 only). */
	public static native void GetFlags(ConVar convar, int flags[2]);
	public static native void SetFlags(ConVar convar, const int flags[2]);

	/** A Float64 ConVar (passed as float). */
	public static native float GetDouble(ConVar convar);
	public static native void SetDouble(ConVar convar, float value);

	public static native void GetVector2(ConVar convar, float value[2]);
	public static native void GetVector3(ConVar convar, float value[3]);
	public static native void GetVector4(ConVar convar, float value[4]);
	public static native void GetQAngle(ConVar convar, float value[3]);
	/** r, g, b, a */
	public static native void GetColor(ConVar convar, int value[4]);
	public static native void SetVector2(ConVar convar, const float value[2]);
	public static native void SetVector3(ConVar convar, const float value[3]);
	public static native void SetVector4(ConVar convar, const float value[4]);
	public static native void SetQAngle(ConVar convar, const float value[3]);
	public static native void SetColor(ConVar convar, const int value[4]);

	/** Invalidates the ConVar (the engine cannot unregister one). */
	public static native void DeleteConVar(ConVar convar);

	/**
	 * Writes cfg/<folder>/<name>.cfg with the plugin's ConVars when it is
	 * missing (autoCreate) and runs it on every map start; OnConfigsExecuted
	 * follows. name "" is the plugin's file name.
	 */
	public static native void AutoExecConfig(bool autoCreate = true, const char[] name = "", const char[] folder = "source2mod");
}
