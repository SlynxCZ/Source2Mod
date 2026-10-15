/**
 * Source2Mod SourcePawn API: files, KeyValues and the streaming KeyValues
 * reader (ISource2ModFiles, ISource2ModKeyValues, ISource2ModKvListener).
 *
 * Paths that are not absolute are under the game folder (csgo); ResolvePath
 * says where one lands. KeyValues is Valve's KV1 text ("key" "value",
 * "section" { ... }), the format of SourceMod's configs and phrase files.
 */

#if defined _source2mod_files_included
 #endinput
#endif
#define _source2mod_files_included

methodmap Source2ModFiles
{
	/** Where a path lands (absolute as given, else under the game folder). @return Bytes written */
	public static native int ResolvePath(const char[] path, char[] buffer, int maxlen);

	public static native bool FileExists(const char[] path);
	public static native bool DirExists(const char[] path);

	/** -1 when there is no such file. */
	public static native int FileSize(const char[] path);

	/** Seconds since 1970 of the last change; -1 when there is no such file. */
	public static native int FileTime(const char[] path);

	/**
	 * The whole file as text.
	 * @return Bytes written, -1 when it cannot be read
	 */
	public static native int ReadFile(const char[] path, char[] buffer, int maxlen);

	/** Creates or replaces (or appends to) a file; missing folders are made. */
	public static native bool WriteFile(const char[] path, const char[] text, bool append = false);

	public static native bool DeleteFile(const char[] path);
	public static native bool RenameFile(const char[] from, const char[] to);

	/** Makes a folder and those above it. */
	public static native bool CreateDirectory(const char[] path);

	/** Removes an empty folder. */
	public static native bool RemoveDirectory(const char[] path);

	/**
	 * The entries of a folder (without "." and ".."), each a string; folders
	 * end in "/". Close the list when done.
	 * @return null when the folder cannot be read
	 */
	public static native ArrayList ListDirectory(const char[] path);
}

/** An open file, read or written a line or a string at a time. delete = close. */
methodmap Source2ModFile < Handle
{
	/**
	 * Opens a file. mode is fopen's: "r", "w", "a", "r+", "w+", "a+", with
	 * "b" for binary. Writing modes make missing folders.
	 * @return null when it cannot be opened
	 */
	public static native Source2ModFile Open(const char[] path, const char[] mode);

	/**
	 * The next line, without its line break.
	 * @return false at the end of the file
	 */
	public native bool ReadLine(char[] buffer, int maxlen);

	/** Writes text as it is. */
	public native bool WriteString(const char[] text);

	/** Formats, then writes the result and a line break. */
	public native bool WriteLine(const char[] format, any ...);

	public native bool EndOfFile();

	/** whence: 0 = from the start, 1 = from here, 2 = from the end. */
	public native bool Seek(int position, int whence = 0);
	public native int Position();
	public native bool Flush();
}

/** Value types of a KeyValues key (ESource2ModKvType). */
enum Source2ModKvType
{
	KvType_None = 0,     /**< a section */
	KvType_String = 1,
	KvType_Int = 2,
	KvType_Float = 3,
	KvType_Color = 6,
	KvType_UInt64 = 7
}

/**
 * A KeyValues tree with a cursor. The cursor starts at the root; JumpToKey
 * and the Goto* methods move it down, GoBack up, Rewind to the root. Keys
 * given to the getters and setters are relative to the cursor and may be
 * paths ("a/b/c"); "" is the cursor's own key. delete frees the whole tree.
 */
methodmap Source2ModKeyValues < Handle
{
	/** An empty tree whose root is called name. */
	public native Source2ModKeyValues(const char[] name);

	/**
	 * Replaces the tree with a file's (the root becomes the file's top
	 * section). escapes: \n \t \\ \" are read as escapes.
	 */
	public native bool LoadFromFile(const char[] path, bool escapes = false);
	public native bool LoadFromString(const char[] text, bool escapes = false);

	/** The tree under the cursor. */
	public native bool SaveToFile(const char[] path, bool escapes = false);

	/** @return Bytes the whole text needs (may be more than maxlen) */
	public native int SaveToString(char[] buffer, int maxlen, bool escapes = false);

	/** The cursor's key name. */
	public native void GetSectionName(char[] buffer, int maxlen);
	public native void SetSectionName(const char[] name);

	/** Moves to a key (path) under the cursor, made when create is set. */
	public native bool JumpToKey(const char[] key, bool create = false);

	/** keysOnly: sections only, else values too. @return false when there is none (the cursor stays) */
	public native bool GotoFirstSubKey(bool keysOnly = true);
	public native bool GotoNextKey(bool keysOnly = true);

	/** One level up. @return false at the root */
	public native bool GoBack();

	/** Back to the root. */
	public native void Rewind();

	/** How deep the cursor is (0 at the root). */
	public native int NodesInStack();

	public native Source2ModKvType GetDataType(const char[] key);

	/** @return Bytes written */
	public native int GetString(const char[] key, char[] buffer, int maxlen, const char[] defvalue = "");
	public native int GetNum(const char[] key, int defvalue = 0);
	public native float GetFloat(const char[] key, float defvalue = 0.0);
	/** As text, e.g. a SteamID64. @return Bytes written */
	public native int GetUInt64(const char[] key, char[] buffer, int maxlen, const char[] defvalue = "0");
	/** r, g, b, a; a "r g b a" string value works too. */
	public native void GetColor(const char[] key, int color[4]);

	public native void SetString(const char[] key, const char[] value);
	public native void SetNum(const char[] key, int value);
	public native void SetFloat(const char[] key, float value);
	public native void SetUInt64(const char[] key, const char[] value);
	public native void SetColor(const char[] key, const int color[4]);

	/** Removes a key (path) under the cursor. */
	public native bool DeleteKey(const char[] key);

	/** Removes the cursor's key and moves to its parent. @return false at the root */
	public native bool DeleteThis();

	/** Copies the keys under other's cursor to under this cursor (replacing what was there). */
	public native void Import(Source2ModKeyValues other);

	/** Adds other's keys under this cursor; existing ones are overwritten. */
	public native void Merge(Source2ModKeyValues other);

	/** A new tree: a copy of the key under the cursor. */
	public native Source2ModKeyValues Clone();
}

/** What a stream callback tells the reader (ESource2ModKvParseResult). */
enum Source2ModKvParseResult
{
	KvParse_Continue = 0,
	KvParse_Halt,      /**< stop, not an error */
	KvParse_HaltFail   /**< stop with KvError_Custom */
}

/** Why a stream stopped (ESource2ModKvError, SourceMod's SMCError numbering). */
enum Source2ModKvError
{
	KvError_Okay = 0,
	KvError_StreamOpen,
	KvError_StreamError,
	KvError_Custom,
	KvError_InvalidSection1,
	KvError_InvalidSection2,
	KvError_InvalidSection3,
	KvError_InvalidSection4,
	KvError_InvalidSection5,
	KvError_InvalidTokens,
	KvError_TokenOverflow,
	KvError_InvalidProperty1
}

typedef Source2ModKvStart = function void (Handle parser);
typedef Source2ModKvEnterSection = function Source2ModKvParseResult (Handle parser, const char[] name, bool quoted);
typedef Source2ModKvKeyValue = function Source2ModKvParseResult (Handle parser, const char[] key, const char[] value, bool keyQuoted, bool valueQuoted);
typedef Source2ModKvLeaveSection = function Source2ModKvParseResult (Handle parser);
typedef Source2ModKvRawLine = function Source2ModKvParseResult (Handle parser, const char[] line, int lineNumber);
typedef Source2ModKvEnd = function void (Handle parser, bool halted, bool failed);

/**
 * Reads KeyValues text without building a tree: the callbacks set are called
 * as the reader goes. Unset ones are skipped.
 */
methodmap Source2ModKvParser < Handle
{
	public native Source2ModKvParser();

	property Source2ModKvStart OnStart
	{
		public native set(Source2ModKvStart fn);
	}
	property Source2ModKvEnterSection OnEnterSection
	{
		public native set(Source2ModKvEnterSection fn);
	}
	property Source2ModKvKeyValue OnKeyValue
	{
		public native set(Source2ModKvKeyValue fn);
	}
	property Source2ModKvLeaveSection OnLeaveSection
	{
		public native set(Source2ModKvLeaveSection fn);
	}
	property Source2ModKvRawLine OnRawLine
	{
		public native set(Source2ModKvRawLine fn);
	}
	property Source2ModKvEnd OnEnd
	{
		public native set(Source2ModKvEnd fn);
	}

	/** line / col: where it stopped. escapes: \n \t \\ \" are read as escapes. */
	public native Source2ModKvError ParseFile(const char[] path, int &line = 0, int &col = 0, bool escapes = true);
	public native Source2ModKvError ParseString(const char[] text, int &line = 0, int &col = 0, bool escapes = true);

	/** @return Bytes written */
	public static native int GetErrorString(Source2ModKvError error, char[] buffer, int maxlen);
}
