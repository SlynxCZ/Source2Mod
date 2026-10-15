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
}
