/**
 * Source2Mod SourcePawn API: gamedata (ISource2ModGameConfig) and calling
 * game functions by address.
 *
 * A Source2ModFunction is a game function the script calls: found by a
 * gamedata entry, a signature of its own, or a vtable slot, with its
 * parameters described once. What C++ does with ADDR_* / GAMECONFIG_RESOLVE
 * and a function pointer type.
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
	Source2ModType_Int64,           /**< passed as decimal text ("123") */
	Source2ModType_Float,
	Source2ModType_Double,          /**< passed as float */
	Source2ModType_Entity,          /**< CEntityInstance*: an entity (index) on the script side, NULL_ENTITY = nullptr */
	Source2ModType_Object,          /**< a schema object pointer: a SchemaObject */
	Source2ModType_String,          /**< const char* */
	Source2ModType_Vector,          /**< Vector* / const Vector&: float[3] */
	Source2ModType_VectorValue,     /**< Vector by value: float[3] */
	Source2ModType_Pointer,         /**< any other pointer: an opaque SchemaObject */
	Source2ModType_BufferString,    /**< CBufferString*: text (a hook can set it with SetBufferString) */
	Source2ModType_Command          /**< const CCommand&: in a hook, GetCommandArgs; not callable */
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
}

/**
 * A game function to call. Close it (delete) when done; it goes with the
 * plugin otherwise.
 */
methodmap Source2ModFunction < Handle
{
	/** A function by gamedata entry ("CCSPlayerController::SwitchTeam"); null when it does not resolve. */
	public static native Source2ModFunction FromGameConfig(const char[] name, Source2ModType returnType);

	/** A function by a signature of the script's own, in a module ("server"). */
	public static native Source2ModFunction FromSignature(const char[] module, const char[] signature, Source2ModType returnType);

	/** A virtual function: called on the object passed as the first parameter, by vtable index. */
	public static native Source2ModFunction FromVirtual(int vtableIndex, Source2ModType returnType);

	/** A function at a core address (ISource2ModAddresses); null when it is not resolved. */
	public static native Source2ModFunction FromAddress(Source2ModAddress address, Source2ModType returnType);

	/** The next parameter; `this` of a member function is the first. */
	public native void AddParam(Source2ModType type);

	/**
	 * Calls it. Arguments follow the parameters (entities and objects as
	 * themselves, vectors as float[3], strings as text).
	 *
	 * @return The return value (int, bool, float, entity, object); 0 for void
	 *         and for the types returned through Call's string form
	 */
	public native any Call(any ...);
}
