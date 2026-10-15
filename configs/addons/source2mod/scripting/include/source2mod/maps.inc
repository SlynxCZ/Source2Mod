/**
 * Source2Mod SourcePawn API: maps (ISource2ModMaps).
 *
 * The current and next map, map changes, map history and map lists. A map is
 * "de_dust2" (a map the server has), "workshop/<id>" or "<id>" (a Workshop
 * map, changed to with host_workshop_map) or any other name (a map of the
 * host's Workshop collection, ds_workshop_changelevel).
 *
 * Map lists come from configs/maplists.json:
 *
 *   {
 *     "mapcyclefile": { "file": "mapcycle.txt" },
 *     "default":      { "target": "mapcyclefile" },
 *     "mapchooser":   { "maps": [ "de_dust2", "workshop/3070212801" ] }
 *   }
 *
 * "file" is relative to the game directory; a list that is missing falls
 * back to "default", then "mapcyclefile", then mapcycle.txt.
 *
 * SourceMod's own names are there as well: GetCurrentMap, IsMapValid,
 * FindMap, GetMapDisplayName, SetNextMap, GetNextMap, ForceChangeLevel,
 * GetMapHistorySize, GetMapHistory, ReadMapList.
 */

#if defined _source2mod_maps_included
 #endinput
#endif
#define _source2mod_maps_included

#include <source2mod/core>
#include <source2mod/adt>

// ReadMapList flags (SourceMod's values).
#define MAPLIST_FLAG_MAPSFOLDER    (1<<0)    /**< On failure, use all maps in the maps folder. */
#define MAPLIST_FLAG_CLEARARRAY    (1<<1)    /**< If an input array is specified, clear it before adding. */
#define MAPLIST_FLAG_NO_DEFAULT    (1<<2)    /**< Do not read "default" or "mapcyclefile" on failure. */

/**
 * What FindMap found. SourceMod's FindMapResult (halflife.inc, (C)2004-2014
 * AlliedModders LLC, GPLv3 with the AlliedModders exception).
 */
enum FindMapResult
{
	FindMap_Found,              /**< A direct match for this name was found */
	FindMap_NotFound,           /**< No match for this map name could be found */
	FindMap_FuzzyMatch,         /**< A fuzzy match was found (de_dus -> de_dust2) */
	FindMap_NonCanonical,       /**< A non-canonical match was found (workshop/1234 -> the Workshop map's full name) */
	FindMap_PossiblyAvailable   /**< No match available now, but it may be possible to load it */
};

methodmap Source2ModMaps
{
	/** The map running now. @return Bytes written. */
	public static native int GetCurrentMap(char[] buffer, int maxlen);

	/** Whether the server has this map (Workshop maps not downloaded yet are not valid). */
	public static native bool IsMapValid(const char[] map);

	/** The map a typed name means; `found` gets its full name. */
	public static native FindMapResult FindMap(const char[] map, char[] found, int maxlen);

	/**
	 * The map the match moves to when it ends. Sets the game's nextlevel; a
	 * Workshop map is changed to by the core when the match ends.
	 *
	 * @return false for a name FindMap() does not find.
	 */
	public static native bool SetNextMap(const char[] map);

	/** What SetNextMap() set, else nextlevel, else the next map of "mapcyclefile". @return false when none. */
	public static native bool GetNextMap(char[] buffer, int maxlen);

	/** Changes the map now; the reason goes into the map history. */
	public static native bool ChangeLevel(const char[] map, const char[] reason = "");

	/** How many earlier maps the history holds (up to 32). */
	public static native int GetMapHistorySize();

	/** An earlier map, 0 = the one before this. @return false when out of range. */
	public static native bool GetMapHistory(int index, char[] map, int mapLen, char[] reason, int reasonLen, int &startTime);

	/**
	 * Reads a map list into `list` (made when null: items of PLATFORM_MAX_PATH
	 * bytes, see source2mod/paths.inc; the caller closes it).
	 *
	 * @param flags  MAPLIST_FLAG_MAPSFOLDER / MAPLIST_FLAG_NO_DEFAULT.
	 * @return       The list, null when nothing could be read.
	 */
	public static native ArrayList ReadMapList(const char[] name = "default", int flags = 0, ArrayList list = null);
}
