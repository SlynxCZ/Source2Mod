/**
 * Source2Mod SourcePawn API: targeting (ISource2ModTargeting).
 *
 * Command text to players: @all, @ct, @t, @spec, @alive, @dead, @bots,
 * @humans, @me, @!me, @aim and filters scripts add; #<userid>, a SteamID
 * in any form, #<exact name>; otherwise part of a name (an exact match wins).
 * The caller's immunity is checked through permissions (CanTarget) unless
 * Target_NoImmunity; the console (slot -1) targets anyone.
 */

#if defined _source2mod_targeting_included
 #endinput
#endif
#define _source2mod_targeting_included

#include <source2mod/types>

// Filters (SourceMod's COMMAND_FILTER_* values).
#define Target_Alive        (1 << 0)
#define Target_Dead         (1 << 1)
#define Target_Connected    (1 << 2)
#define Target_NoImmunity   (1 << 3)
#define Target_NoMulti      (1 << 4)
#define Target_NoBots       (1 << 5)

// Results: > 0 is the number of players.
#define Target_None         0
#define Target_NotAlive     -1
#define Target_NotDead      -2
#define Target_NotInGame    -3
#define Target_Immune       -4
#define Target_EmptyFilter  -5
#define Target_NotHuman     -6
#define Target_Ambiguous    -7

/**
 * A filter a script adds ("@admins"): add the slots it means to targets.
 * @return false when it cannot be used now (it then matches nobody)
 */
typedef Source2ModTargetFilter = function bool (CPlayerSlot caller, ArrayList targets);

methodmap Source2ModTargeting
{
	/**
	 * The players a pattern means.
	 *
	 * @param caller   Who is targeting; CPlayerSlot(-1) for the console.
	 * @param targets  Filled with player slots (ints), in slot order.
	 * @param name     How to name them: a phrase in common.phrases when
	 *                 nameIsPhrase ("all terrorists"), else a player's name.
	 * @return The number of players, or a Target_* error (<= 0)
	 */
	public static native int ProcessTargetString(const char[] pattern, CPlayerSlot caller, int flags, int[] targets, int maxTargets,
	                                             char[] name = "", int nameLen = 0, bool &nameIsPhrase = false);

	/** One player into target, or an error (<= 0). */
	public static native int FindTarget(const char[] pattern, CPlayerSlot caller, int flags, CPlayerSlot &target);

	/** The phrase in common.phrases for an error ("No matching client", ...). @return Bytes written */
	public static native int GetTargetErrorPhrase(int error, char[] buffer, int maxlen);

	/** The same in English. @return Bytes written */
	public static native int GetTargetErrorText(int error, char[] buffer, int maxlen);

	/**
	 * Adds "@<name>"; phrase names the targets ("all admins"), a common.phrases
	 * name when phraseIsTranslation.
	 * @return An id for RemoveTargetFilter, 0 when the name is taken
	 */
	public static native int AddTargetFilter(const char[] name, Source2ModTargetFilter filter, const char[] phrase, bool phraseIsTranslation = false);
	public static native bool RemoveTargetFilter(int id);
}
