/**
 * Source2Mod SourcePawn API: files (ISource2ModFiles).
 *
 * Paths that are not absolute are under the game folder (csgo). For files
 * opened line by line, KeyValues trees and the streaming KeyValues reader,
 * SourceMod's File, KeyValues and SMCParser methodmaps (files.inc,
 * keyvalues.inc, textparse.inc) are the SourcePawn side of the same core
 * code; these are the whole-file helpers next to them.
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
