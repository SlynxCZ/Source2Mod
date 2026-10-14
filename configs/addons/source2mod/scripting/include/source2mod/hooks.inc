/**
 * Source2Mod SourcePawn API: raw hooks on game functions (KHook from C++:
 * KHOOK_MEMBER / KHOOK_FUNCTION / KHOOK_VIRTUAL).
 *
 * For what the game hooks (source2mod/gamehooks) do not cover: any function
 * found by gamedata, by a signature of the script's own, or by vtable slot.
 * The parameters are described with Source2ModType, as for
 * Source2ModFunction. Handlers read and change the arguments and the return
 * value through the objects they are handed, valid inside the handler.
 */

#if defined _source2mod_hooks_included
 #endinput
#endif
#define _source2mod_hooks_included

#include <source2mod/types>
#include <source2mod/gameconfig>
#include <source2mod/commands>

/** The arguments of the hooked call. Numbered from 1; `this` is 1 for a member function. */
methodmap Source2ModHookParams
{
	public native int GetInt(int num);
	public native float GetFloat(int num);
	public native bool GetBool(int num);
	public native CEntityInstance GetEntity(int num);
	public native SchemaObject GetObject(int num);
	/**
	 * Text of a String, BufferString or Int64 (decimal) parameter, or the
	 * whole command line of a Command parameter.
	 */
	public native int GetString(int num, char[] buffer, int maxlen);
	public native void GetVector(int num, float vec[3]);

	/** Changes take effect with Action_Override / Action_Supersede (pre). */
	public native void SetInt(int num, int value);
	public native void SetFloat(int num, float value);
	public native void SetBool(int num, bool value);
	public native void SetEntity(int num, CEntityInstance entity);
	public native void SetVector(int num, const float vec[3]);

	/** A BufferString parameter: replaces its text in place (no Action needed). */
	public native void SetBufferString(int num, const char[] value);

	/** A Command parameter as command arguments, Arg(0) the command; valid inside the handler. */
	public native Source2ModCommandArgs GetCommandArgs(int num);
}

/** The return value: what the original returned (post), what the caller gets when overridden. */
methodmap Source2ModHookReturn
{
	public native int GetInt();
	public native float GetFloat();
	public native bool GetBool();
	public native CEntityInstance GetEntity();
	public native void SetInt(int value);
	public native void SetFloat(float value);
	public native void SetBool(bool value);
	public native void SetEntity(CEntityInstance entity);
}

typedef Source2ModRawHookHandler = function Source2ModAction (Source2ModHookParams params, Source2ModHookReturn ret, bool post);

/**
 * One hooked function. Enable() installs it, Disable() or delete takes it
 * down; it goes with the plugin otherwise.
 */
methodmap Source2ModHook < Handle
{
	/** By gamedata entry; null when it does not resolve. */
	public static native Source2ModHook FromGameConfig(const char[] name, Source2ModType returnType);

	/** By a signature of the script's own, in a module ("server"). */
	public static native Source2ModHook FromSignature(const char[] module, const char[] signature, Source2ModType returnType);

	/**
	 * A virtual function of every object with this vtable: an RTTI class
	 * name in a module, and the slot.
	 */
	public static native Source2ModHook FromVirtual(const char[] module, const char[] vtableName, int vtableIndex, Source2ModType returnType);

	/** The next parameter; `this` of a member function is the first. */
	public native void AddParam(Source2ModType type);

	/** Installs the hook; either handler may be INVALID_FUNCTION. */
	public native bool Enable(Source2ModRawHookHandler pre = INVALID_FUNCTION, Source2ModRawHookHandler post = INVALID_FUNCTION);

	public native void Disable();
}
