/**
 * Source2Mod SourcePawn API: entities (ISource2ModEntities, IEntityListener,
 * IEntityIOListener).
 */

#if defined _source2mod_entities_included
 #endinput
#endif
#define _source2mod_entities_included

#include <source2mod/types>
#include <source2mod/schema>

/** IEntityListener's callbacks, one function each. */
typedef Source2ModEntityCallback = function void (CEntityInstance entity);
typedef Source2ModEntityParentCallback = function void (CEntityInstance entity, CEntityInstance newParent);

/**
 * IEntityIOListener::OnEntityOutput. Pre may answer Action_Supersede: the
 * output then never fires.
 */
typedef Source2ModEntityOutputHandler = function Source2ModAction (const char[] output, CEntityInstance activator, CEntityInstance caller, float delay, bool post);

methodmap Source2ModEntities
{
	/** The game rules, kept at hand by the core; null between maps. */
	public static native CCSGameRules GetGameRules();

	/** What the player's crosshair is on; NULL_ENTITY for nothing. */
	public static native CBaseEntity FindPickerEntity(CBasePlayerController player);

	/** The next entity of a class after `start` (NULL_ENTITY starts the search). */
	public static native CBaseEntity FindEntityByClassname(CEntityInstance start, const char[] className);

	/** The next entity with a targetname after `start` (NULL_ENTITY starts the search). */
	public static native CBaseEntity FindEntityByName(CEntityInstance start, const char[] name);

	/** Created, not spawned: set it up, then DispatchSpawn(). */
	public static native CBaseEntity CreateEntityByName(const char[] className);

	/**
	 * IEntityListener: created (keyvalues not applied, not spawned), spawned,
	 * deleted (the last call for it), parent changed. Pass INVALID_FUNCTION
	 * for what is not needed.
	 *
	 * @return The id RemoveEntityListener takes
	 */
	public static native int AddEntityListener(Source2ModEntityCallback created = INVALID_FUNCTION,
	                                           Source2ModEntityCallback spawned = INVALID_FUNCTION,
	                                           Source2ModEntityCallback deleted = INVALID_FUNCTION,
	                                           Source2ModEntityParentCallback parentChanged = INVALID_FUNCTION);

	public static native bool RemoveEntityListener(int id);

	/** An input, as a map's I/O sends it. */
	public static native void AcceptInput(CEntityInstance target, const char[] input, CEntityInstance activator = NULL_ENTITY,
	                                      CEntityInstance caller = NULL_ENTITY, const char[] value = "");

	/** The same, queued: runs after `delay` seconds. */
	public static native void AddEntityIOEvent(CEntityInstance target, const char[] input, CEntityInstance activator = NULL_ENTITY,
	                                           CEntityInstance caller = NULL_ENTITY, const char[] value = "", float delay = 0.0);

	/**
	 * Outputs of a class ("" for any class) and an output name ("" for any).
	 *
	 * @return The id RemoveEntityIOListener takes
	 */
	public static native int AddEntityIOListener(Source2ModEntityOutputHandler handler, const char[] className, const char[] outputName, bool post = false);

	public static native bool RemoveEntityIOListener(int id);
}
