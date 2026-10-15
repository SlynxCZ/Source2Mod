/**
 * Source2Mod SourcePawn API: entities and the schema classes.
 *
 * Every class of the SDK's schema (schema/entity/classes) is a methodmap of
 * the same name deriving from the same base (schema/classes.inc, generated).
 * Entity classes -- CEntityInstance and everything under it -- hold the
 * entity index, so view_as<CCSPlayerPawn>(index) is a pawn and an entity
 * methodmap converts to its index with view_as<int>. Every other class
 * (services, the scene node, the game rules) is a SchemaObject: a reference
 * the core hands out, good as long as what it belongs to exists.
 *
 * Fields are properties named like the C++ accessors (pawn.m_iHealth = 50;
 * a write to a networked field is sent to the clients). A field that cannot
 * be a property -- vectors, colors, strings -- is a method of its name that
 * fills a buffer, and Set_<name>() writes it. Handles read as the entity
 * they point at, NULL_ENTITY when empty.
 */

#if defined _source2mod_schema_included
 #endinput
#endif
#define _source2mod_schema_included

#include <source2mod/types>
#include <source2mod/events>
#include <source2mod/adt>

/** No entity: an empty handle, a failed lookup. */
#define NULL_ENTITY view_as<any>(-1)

/**
 * A schema object that is not an entity (services, the scene node, the game
 * rules). 0 is null.
 */
methodmap SchemaObject
{
	property bool IsNull
	{
		public get() { return view_as<int>(this) == 0; }
	}
}

/**
 * The root of every entity class.
 */
methodmap CEntityInstance
{
	public CEntityInstance(int index)
	{
		return view_as<CEntityInstance>(index);
	}

	/** The entity index. */
	public int GetEntityIndex()
	{
		return view_as<int>(this);
	}

	/** Whether an entity lives at this index. */
	public native bool IsValid();

	/** The designer name ("weapon_ak47", "cs_player_controller"). */
	public native int GetClassname(char[] buffer, int maxlen);

	/** The entity's targetname, "" when it has none. */
	public native int GetName(char[] buffer, int maxlen);

	/**
	 * A reference that stays bound to this entity: FromRefEHandle() gives
	 * NULL_ENTITY once it is gone, even when its index has been reused. What
	 * to keep across frames.
	 */
	public native int GetRefEHandle();

	public static native CEntityInstance FromRefEHandle(int ref);
}

/**
 * Generic access to a schema field, by class and field name -- what the
 * generated properties are made of, for the fields they do not cover.
 * `instance` is an entity (index) or a SchemaObject.
 */
methodmap Source2ModSchema
{
	public static native int GetInt(any instance, const char[] className, const char[] field);
	public static native void SetInt(any instance, const char[] className, const char[] field, int value);
	public static native float GetFloat(any instance, const char[] className, const char[] field);
	public static native void SetFloat(any instance, const char[] className, const char[] field, float value);
	public static native bool GetBool(any instance, const char[] className, const char[] field);
	public static native void SetBool(any instance, const char[] className, const char[] field, bool value);

	/** A CHandle field: the entity index, -1 when empty. */
	public static native int GetHandle(any instance, const char[] className, const char[] field);
	public static native void SetHandle(any instance, const char[] className, const char[] field, int entity);

	/** A pointer to an entity: its index, -1 when null. */
	public static native int GetEntityPointer(any instance, const char[] className, const char[] field);

	/** A pointer to, or an embedded, schema object: its SchemaObject, 0 when null. */
	public static native SchemaObject GetObject(any instance, const char[] className, const char[] field);

	public static native void GetVector(any instance, const char[] className, const char[] field, float vec[3]);
	public static native void SetVector(any instance, const char[] className, const char[] field, const float vec[3]);
	public static native void GetColor(any instance, const char[] className, const char[] field, int color[4]);
	public static native void SetColor(any instance, const char[] className, const char[] field, const int color[4]);

	/** char arrays, CUtlSymbolLarge, CUtlString; only char arrays can be set. */
	public static native int GetString(any instance, const char[] className, const char[] field, char[] buffer, int maxlen);
	public static native void SetString(any instance, const char[] className, const char[] field, const char[] value);

	/** The field's offset in its class, -1 when the running game has no such field. */
	public static native int GetOffset(const char[] className, const char[] field);

	/**
	 * Tells the clients a networked field changed (NetworkStateChanged) -- for
	 * a field written behind the setters' back: raw memory, an array element,
	 * a game function. The setters above do it themselves.
	 */
	public static native void StateChanged(any instance, const char[] className, const char[] field);

	/**
	 * Raw memory `offset` bytes into an entity or SchemaObject (C++
	 * ISource2ModMemory's GetValue / Offset / Deref): for members the schema
	 * does not describe. Nothing is checked -- a wrong offset reads or writes
	 * the wrong thing.
	 */
	public static native int GetIntAt(any instance, int offset, int size = 4, bool isSigned = true);
	public static native void SetIntAt(any instance, int offset, int value, int size = 4);
	/** 64 bits as two cells, value[0] the low half. */
	public static native void GetInt64At(any instance, int offset, int value[2]);
	public static native void SetInt64At(any instance, int offset, const int value[2]);
	public static native float GetFloatAt(any instance, int offset);
	public static native void SetFloatAt(any instance, int offset, float value);
	public static native void GetVectorAt(any instance, int offset, float vec[3]);
	public static native void SetVectorAt(any instance, int offset, const float vec[3]);
	/** A CHandle at the offset, as the entity; NULL_ENTITY when empty. */
	public static native int GetEntityAt(any instance, int offset);
	public static native void SetEntityAt(any instance, int offset, int entity);
	/** The pointer stored at the offset (Deref), 0 when null. */
	public static native SchemaObject GetObjectAt(any instance, int offset);
	/** The object that starts at the offset (Offset). */
	public static native SchemaObject GetEmbeddedAt(any instance, int offset);
	/** A char* stored at the offset (CUtlSymbolLarge, CUtlString, const char*). */
	public static native int GetStringPointerAt(any instance, int offset, char[] buffer, int maxlen);

	/** A CUtlVector field (networked ones too): how many elements. */
	public static native int GetUtlVectorCount(any instance, const char[] className, const char[] field);

	/**
	 * Element `index` of a CUtlVector field, `elementSize` bytes each, as a
	 * SchemaObject: view_as a schema class for a vector of objects, or read it
	 * with the *At natives (GetObjectAt(element, 0) for a vector of pointers,
	 * GetEntityAt(element, 0) for one of handles).
	 */
	public static native SchemaObject GetUtlVectorElement(any instance, const char[] className, const char[] field, int index, int elementSize);
}

/**
 * IEntityIOListener::OnEntityOutput (Source2ModEntities.AddEntityIOListener,
 * CBaseEntity.AddSingleEntityIOListener). Pre may answer Action_Supersede:
 * the output then never fires.
 */
typedef Source2ModEntityOutputHandler = function Source2ModAction (const char[] output, CEntityInstance activator, CEntityInstance caller, float delay, bool post);

#include <source2mod/schema/classes>
