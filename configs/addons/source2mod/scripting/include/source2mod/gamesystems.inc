/**
 * Source2Mod SourcePawn API: game systems (ISource2ModGameSystems).
 *
 * A game system is an engine object a script reaches only to call into it
 * (Source2ModFunction.FromVirtual with the system as the first argument).
 */

#if defined _source2mod_gamesystems_included
 #endinput
#endif
#define _source2mod_gamesystems_included

#include <source2mod/schema>

methodmap Source2ModGameSystems
{
	/** Whether the game system list could be found on the running game. */
	public static native bool IsAvailable();

	/**
	 * A game system by its factory name ("SpawnGroupManagerGameSystem"); a null
	 * SchemaObject when there is none or its factory has no static instance.
	 */
	public static native SchemaObject FindByName(const char[] name);
}
