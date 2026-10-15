/**
 * vim: set ts=4 sw=4 tw=99 noet :
 * ======================================================
 * Source2Mod C++ Sample Plugin -- script natives
 * ======================================================
 *
 * What cpp_sample registers for scripts in section 12 (SetupScriptNatives()),
 * the way a SourceMod extension ships its .inc. Optional: a script that
 * includes this still loads without cpp_sample and asks Source2ModNatives.LibraryExists().
 *
 * This sample plugin is public domain.
 */

#if defined _cpp_sample_included
	#endinput
#endif
#define _cpp_sample_included

/**
 * a + b, worked out in C++.
 */
native int Sample_Add(int a, int b);

/**
 * Formats like Format(), through the core's formatter, from C++.
 *
 * @return              Bytes written.
 */
native int Sample_Echo(char[] buffer, int maxlen, const char[] fmt, any ...);

/**
 * A counter starting at `start`, as a handle of cpp_sample's own type.
 * Close it with delete; it is closed for you when the script unloads.
 */
native Handle Sample_CreateCounter(int start);

/**
 * Adds one to the counter and returns the new value.
 */
native int Sample_Increment(Handle counter);

public SharedPlugin __pl_cpp_sample =
{
	name = "cpp_sample",
	file = "cpp_sample.smx",
	required = 0,
};

#if !defined REQUIRE_PLUGIN
public void __pl_cpp_sample_SetNTVOptional()
{
	MarkNativeAsOptional("Sample_Add");
	MarkNativeAsOptional("Sample_Echo");
	MarkNativeAsOptional("Sample_CreateCounter");
	MarkNativeAsOptional("Sample_Increment");
}
#endif
