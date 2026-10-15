/**
 * Source2Mod SourcePawn API: raw hooks on game functions (KHook from C++:
 * KHOOK_MEMBER / KHOOK_FUNCTION / KHOOK_VIRTUAL).
 *
 * For what the game hooks (source2mod/gamehooks) do not cover: any function
 * found by gamedata, by a signature or symbol of the script's own, or by
 * vtable slot. The parameters are described with Source2ModType, as for
 * Source2ModFunction (and with the same limits on how many). Handlers read
 * and change the arguments and the return value through the objects they
 * are handed, valid inside the handler.
 *
 * A hook is a Handle, not an id: it is described (AddParam) before it is
 * installed, and Enable / Disable can be repeated.
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
	/** An int; for an IntRef / BoolRef / FloatRef out-parameter the value it points at. */
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
	/** An Int64 parameter as decimal text. */
	public native int GetInt64(int num, char[] buffer, int maxlen);
	public native void GetVector(int num, float vec[3]);

	/**
	 * Changes take effect with Action_Override / Action_Supersede (pre).
	 * An IntRef / BoolRef / FloatRef out-parameter is written in place
	 * instead (no Action needed), as SetVector does.
	 */
	public native void SetInt(int num, int value);
	public native void SetFloat(int num, float value);
	public native void SetBool(int num, bool value);
	public native void SetEntity(int num, CEntityInstance entity);
	public native void SetVector(int num, const float vec[3]);
	/** An Object or Pointer parameter; 0 for null. */
	public native void SetObject(int num, SchemaObject value);
	/** A String parameter: points at a copy that lives until the hooked call returns. */
	public native void SetString(int num, const char[] value);
	/** An Int64 parameter from decimal text. */
	public native void SetInt64(int num, const char[] value);

	/** A BufferString parameter: replaces its text in place (no Action needed). */
	public native void SetBufferString(int num, const char[] value);

	/** A Command parameter as command arguments, Arg(0) the command; valid inside the handler. */
	public native Source2ModCommandArgs GetCommandArgs(int num);

	/**
	 * Pre only: runs the original now, past every hook, with the current
	 * (possibly changed) parameters, and makes its value the return value.
	 * Answer Action_Supersede after it, or it runs twice.
	 *
	 * @return What it returned (int, bool, float, entity, object); 0 for void
	 */
	public native any CallOriginal();
}

/** The return value: what the original returned (post), what the caller gets when overridden. */
methodmap Source2ModHookReturn
{
	public native int GetInt();
	public native float GetFloat();
	public native bool GetBool();
	public native CEntityInstance GetEntity();
	/** An Object or Pointer return value. */
	public native SchemaObject GetObject();
	/** An integer return value as decimal text. */
	public native int GetInt64(char[] buffer, int maxlen);
	public native void SetInt(int value);
	public native void SetFloat(float value);
	public native void SetBool(bool value);
	public native void SetEntity(CEntityInstance entity);
	/** 0 for null. */
	public native void SetObject(SchemaObject value);
	/** From decimal text. */
	public native void SetInt64(const char[] value);
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

	/** By a signature of the script's own, in a module ("server"), optionally only in one section (".text"). */
	public static native Source2ModHook FromSignature(const char[] module, const char[] signature, Source2ModType returnType, const char[] section = "");

	/** The function a matched call / jmp / lea refers to: see Source2ModFunction.FromSignatureRelative. */
	public static native Source2ModHook FromSignatureRelative(const char[] module, const char[] signature, Source2ModType returnType,
	                                                          int offset = 1, int nextInstructionOffset = 5, const char[] section = "");

	/** By its exported or debug symbol in a module ("server"); null when there is none. */
	public static native Source2ModHook FromSymbol(const char[] module, const char[] symbol, Source2ModType returnType);

	/**
	 * A virtual function of every object with this vtable: an RTTI class
	 * name in a module, and the slot. baseName picks the vtable of a
	 * secondary base (KHOOK_VTABLE_BASE).
	 */
	public static native Source2ModHook FromVirtual(const char[] module, const char[] vtableName, int vtableIndex, Source2ModType returnType, const char[] baseName = "");

	/**
	 * A virtual function in a secondary vtable of a class with multiple
	 * bases: the one whose offset-to-top is `offset` (0 the primary,
	 * negative for the others as the ABI stores it).
	 */
	public static native Source2ModHook FromVirtualSecondary(const char[] module, const char[] vtableName, int offset, int vtableIndex, Source2ModType returnType);

	/**
	 * A virtual function of one object (an entity or a SchemaObject): the
	 * vtable it uses is hooked, the handlers run only for calls on it. Disable
	 * it before the object goes; a new object at the same address would match.
	 */
	public static native Source2ModHook FromVirtualObject(any instance, int vtableIndex, Source2ModType returnType);

	/** A function at a core address (ISource2ModAddresses); null when it is not resolved. */
	public static native Source2ModHook FromAddress(Source2ModAddress address, Source2ModType returnType);

	/** The next parameter; `this` of a member function is the first. */
	public native void AddParam(Source2ModType type);

	/** Installs the hook; either handler may be INVALID_FUNCTION. */
	public native bool Enable(Source2ModRawHookHandler pre = INVALID_FUNCTION, Source2ModRawHookHandler post = INVALID_FUNCTION);

	public native void Disable();

	/** Whether the hook is installed (ISource2ModHook::IsInstalled). */
	property bool Enabled
	{
		public native get();
	}
}
