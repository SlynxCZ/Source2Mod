/**
 * Source2Mod SourcePawn API: text, time and logging helpers (ISource2ModUtils).
 *
 * The same helpers C++ plugins get. Regex, profiling, sorting and IntMap are
 * SourceMod's methodmaps and natives (regex.inc, profiler.inc, sorting.inc,
 * adt_trie.inc) on the same core code.
 */

#if defined _source2mod_utils_included
 #endinput
#endif
#define _source2mod_utils_included

methodmap Source2ModUtils
{
	/**
	 * Splits text on separator into an ArrayList of strings (block size
	 * maxlen); maxParts > 0 caps the parts, the last keeps the rest. Close it.
	 */
	public static native ArrayList Explode(const char[] text, const char[] separator, int maxlen = 256, int maxParts = 0);

	/** The strings of an ArrayList joined. @return Bytes written */
	public static native int Implode(ArrayList parts, const char[] separator, char[] buffer, int maxlen);

	/** @return Replacements made */
	public static native int Replace(char[] text, int maxlen, const char[] search, const char[] replace, bool caseSensitive = true);

	/** Without the whitespace at both ends. @return Bytes written */
	public static native int Trim(const char[] text, char[] buffer, int maxlen);

	/** -1 when search is not in text. */
	public static native int Find(const char[] text, const char[] search, bool caseSensitive = true);

	/** Seconds since 1970. */
	public static native int GetTime();

	/** strftime in the server's local time; stamp -1 is now. @return Bytes written */
	public static native int FormatTime(char[] buffer, int maxlen, const char[] format, int stamp = -1);

	/** The opposite (local time); -1 when text does not fit format. */
	public static native int ParseTime(const char[] text, const char[] format);

	/** Milliseconds on a steady clock (only differences mean anything). */
	public static native int GetTickCount();

	/**
	 * Appends "L <date> - <time>: [<plugin>] <message>" to a file (under the
	 * game folder unless absolute); missing folders are made.
	 */
	public static native bool LogToFile(const char[] path, const char[] format, any ...);
}
