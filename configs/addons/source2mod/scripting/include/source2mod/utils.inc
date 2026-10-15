/**
 * Source2Mod SourcePawn API: text, time, sorting, regex, profiling and
 * logging helpers (ISource2ModUtils).
 *
 * The same helpers C++ plugins get. ArrayList sorting is ArrayList's own
 * (Sort / SortCustom); these sort plain arrays.
 */

#if defined _source2mod_utils_included
 #endinput
#endif
#define _source2mod_utils_included

#include <source2mod/core>
#include <source2mod/adt>

/** How Sort* orders. */
enum Source2ModSortOrder
{
	SortOrder_Ascending = 0,
	SortOrder_Descending = 1,
	SortOrder_Random = 2
}

/**
 * Compares two elements of the array being sorted: < 0 when a goes first,
 * > 0 when b does, 0 when equal.
 */
typedef Source2ModSortFunc = function int (int a, int b, const int[] array, any data);

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

	public static native bool Equal(const char[] a, const char[] b, bool caseSensitive = true);

	/** The string's length in bytes (SourceMod's strlen). */
	public static native int Length(const char[] text);

	/** Copies source into dest, cut to maxlen. @return Bytes written (strcopy) */
	public static native int Copy(char[] dest, int maxlen, const char[] source);

	/** < 0, 0, > 0 like C's strcmp (SourceMod's strcmp). */
	public static native int Compare(const char[] a, const char[] b, bool caseSensitive = true);

	/** The same over the first num characters (strncmp). */
	public static native int CompareN(const char[] a, const char[] b, int num, bool caseSensitive = true);

	/** @return Bytes written */
	public static native int IntToString(int num, char[] buffer, int maxlen);

	/** The number at the start of the text, 0 for none. */
	public static native int StringToInt(const char[] text, int base = 10);

	/** @return Bytes written */
	public static native int FloatToString(float num, char[] buffer, int maxlen);

	public static native float StringToFloat(const char[] text);

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

	public static native void SortInts(int[] array, int size, Source2ModSortOrder order = SortOrder_Ascending);
	public static native void SortFloats(float[] array, int size, Source2ModSortOrder order = SortOrder_Ascending);

	/** Byte-wise (strcmp) order. */
	public static native void SortStrings(char[][] array, int count, Source2ModSortOrder order = SortOrder_Ascending);

	/** func compares two values of array. */
	public static native void SortCustom(int[] array, int size, Source2ModSortFunc func, any data = 0);
}

/** Regex flags (SOURCE2MOD_REGEX_*). */
#define RegexFlag_Caseless  0x01
#define RegexFlag_Multiline 0x02   /**< ^ and $ match at line breaks */
#define RegexFlag_DotAll    0x04   /**< . matches line breaks too */

/**
 * A compiled pattern (ECMAScript syntax, as std::regex) and its last matches.
 * delete releases it.
 */
methodmap Source2ModRegex < Handle
{
	/** @return null (and why in error) for a bad pattern */
	public static native Source2ModRegex Compile(const char[] pattern, int flags = 0, char[] error = "", int maxlen = 0);

	/**
	 * Finds the first match at or after offset (bytes).
	 * @return Substrings of the match (whole match + captures); 0 for none
	 */
	public native int Match(const char[] text, int offset = 0);

	/** Finds every match. @return How many */
	public native int MatchAll(const char[] text);

	/** Matches found by the last Match / MatchAll. */
	public native int MatchCount();

	/** Substrings of a match (whole match + captures). */
	public native int CaptureCount(int match = 0);

	/** Substring index of a match (0 = the whole match). @return false out of range */
	public native bool GetSubString(int index, char[] buffer, int maxlen, int match = 0);

	/** Where a match ends in the text (bytes). */
	public native int MatchEnd(int match = 0);
}

/** A stopwatch. delete releases it. */
methodmap Source2ModProfiler < Handle
{
	public native Source2ModProfiler();
	public native void Start();
	public native void Stop();

	/** Seconds between the last Start and Stop (or now, while running). */
	property float Time
	{
		public native get();
	}
}
