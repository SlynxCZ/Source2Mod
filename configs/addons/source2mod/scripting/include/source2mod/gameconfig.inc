/**
 * Source2Mod SourcePawn API: gamedata (ISource2ModGameConfig) and calling
 * game functions by address.
 *
 * A Source2ModFunction is a game function the script calls: found by a
 * gamedata entry, a signature or symbol of its own, or a vtable slot, with
 * its parameters described once. What C++ does with ADDR_* /
 * GAMECONFIG_RESOLVE and a function pointer type.
 *
 * Arguments: on Windows at most 14; on Linux 6 integer-class (ints, entities,
 * objects, pointers, strings) and 8 floating-point ones in registers, then 8
 * more of either kind on the stack.
 */

#if defined _source2mod_gameconfig_included
 #endinput
#endif
#define _source2mod_gameconfig_included

#include <source2mod/types>
#include <source2mod/addresses>

/**
 * How a value crosses into a game function and back.
 */
enum Source2ModType
{
	Source2ModType_Void = 0,
	Source2ModType_Bool,
	Source2ModType_Int8,
	Source2ModType_Int16,
	Source2ModType_Int32,           /**< also enums, uint32 */
	Source2ModType_Int64,           /**< passed as decimal text ("123"); returned through CallInt64 */
	Source2ModType_Float,
	Source2ModType_Double,          /**< passed as float */
	Source2ModType_Entity,          /**< CEntityInstance*: an entity (handle) on the script side, NULL_ENTITY = nullptr */
	Source2ModType_Object,          /**< a schema object pointer: a SchemaObject */
	Source2ModType_String,          /**< const char*; returned through CallString */
	Source2ModType_Vector,          /**< Vector* / const Vector&: float[3], written back after the call; returned through CallVector */
	Source2ModType_VectorValue,     /**< Vector by value: float[3]; returned through CallVector */
	Source2ModType_Pointer,         /**< any other pointer: an opaque SchemaObject */
	Source2ModType_BufferString,    /**< CBufferString*: text (a hook can set it with SetBufferString) */
	Source2ModType_Command,         /**< const CCommand&: in a hook, GetCommandArgs; not callable */
	Source2ModType_IntRef,          /**< int32* out-parameter: an int variable, written back after the call */
	Source2ModType_BoolRef,         /**< bool* out-parameter: a bool variable, written back after the call */
	Source2ModType_FloatRef         /**< float* out-parameter: a float variable, written back after the call */
};

/**
 * The platform the server runs on, for functions whose parameters differ
 * between the two (CCSGameRules::TerminateRound, GroundAccelerate).
 */
enum Source2ModPlatform
{
	Platform_Linux = 0,
	Platform_Windows
};

methodmap Source2ModGameConfig
{
	/** @return false when the entry does not exist */
	public static native bool GetLibrary(const char[] name, char[] buffer, int maxlen);
	public static native bool GetSignature(const char[] name, char[] buffer, int maxlen);
	public static native bool GetSymbol(const char[] name, char[] buffer, int maxlen);
	public static native bool GetPatch(const char[] name, char[] buffer, int maxlen);

	/** @return The offset, -1 when there is none */
	public static native int GetOffset(const char[] name);

	public static native bool IsSymbol(const char[] name);

	/** Whether the entry's signature or symbol is found on the running game. */
	public static native bool Resolves(const char[] name);

	/**
	 * Whether a signature of the script's own matches in a module ("server"),
	 * optionally only in one section (".text").
	 */
	public static native bool SignatureMatches(const char[] module, const char[] signature, const char[] section = "");

	public static native Source2ModPlatform GetPlatform();
}

/**
 * A game function to call. Close it (delete) when done; it goes with the
 * plugin otherwise.
 */
methodmap Source2ModFunction < Handle
{
	/** A function by gamedata entry ("CCSPlayerController::SwitchTeam"); null when it does not resolve. */
	public static native Source2ModFunction FromGameConfig(const char[] name, Source2ModType returnType);

	/**
	 * A function by a signature of the script's own, in a module ("server"),
	 * optionally only in one section (".text"); null when it does not match.
	 */
	public static native Source2ModFunction FromSignature(const char[] module, const char[] signature, Source2ModType returnType, const char[] section = "");

	/**
	 * A function a matched instruction refers to: the signature matches a
	 * call / jmp / lea, the function is its rel32 target. The default is a
	 * call or jmp at the start of the match (ISource2ModMemory::FollowNearCall);
	 * for others give where the rel32 is and where the next instruction starts,
	 * both from the start of the match (ResolveRelativeAddress).
	 */
	public static native Source2ModFunction FromSignatureRelative(const char[] module, const char[] signature, Source2ModType returnType,
	                                                              int offset = 1, int nextInstructionOffset = 5, const char[] section = "");

	/** A function by its exported or debug symbol in a module ("server"); null when there is none. */
	public static native Source2ModFunction FromSymbol(const char[] module, const char[] symbol, Source2ModType returnType);

	/** A virtual function: called on the object passed as the first parameter, by vtable index. */
	public static native Source2ModFunction FromVirtual(int vtableIndex, Source2ModType returnType);

	/**
	 * A slot of one class's vtable (an RTTI class name in a module), called
	 * whatever the object's own class is; baseName picks the vtable of a
	 * secondary base. The object is still the first parameter.
	 */
	public static native Source2ModFunction FromVirtualTable(const char[] module, const char[] vtableName, int vtableIndex, Source2ModType returnType, const char[] baseName = "");

	/** A function at a core address (ISource2ModAddresses); null when it is not resolved. */
	public static native Source2ModFunction FromAddress(Source2ModAddress address, Source2ModType returnType);

	/** The next parameter; `this` of a member function is the first. */
	public native void AddParam(Source2ModType type);

	/**
	 * Whether every Call* goes past the hooks on the function (all plugins',
	 * raw and game hooks), as CallOriginal does: C++'s HookChain::Bypass.
	 */
	property bool BypassHooks
	{
		public native get();
		public native set(bool bypass);
	}

	/**
	 * Calls it. Arguments follow the parameters (entities and objects as
	 * themselves, vectors as float[3], strings as text, out-parameters as
	 * variables).
	 *
	 * @return The return value (int, bool, float, entity, object); 0 for void.
	 *         An Int64 gives its low 32 bits (CallInt64 gives all); a String
	 *         or Vector return is an error (CallString, CallVector).
	 */
	public native any Call(any ...);

	/**
	 * Calls the function past every hook on it (SOURCE2MOD_ORIGINAL): what a
	 * hook of this function calls to run the game's code without recursing.
	 */
	public native any CallOriginal(any ...);

	/** Calls it and puts an integer return value in buffer as decimal text. @return bytes written */
	public native int CallInt64(char[] buffer, int maxlen, any ...);

	/** Calls a function returning a String; "" for null. @return bytes written */
	public native int CallString(char[] buffer, int maxlen, any ...);

	/**
	 * Calls a function returning a Vector (by value) or Vector* (0,0,0 for
	 * null). On Windows a Vector by value comes back through a hidden pointer
	 * placed after `this` when the function is FromVirtual / FromVirtualTable
	 * or its first parameter is an Entity, Object or Pointer, first otherwise.
	 */
	public native void CallVector(float vec[3], any ...);
}
