/**
* vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * Copyright (C) 2025-2026 Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl.
 * All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 * gives you permission to link the code of this program
 * (as well as its derivative works) to "Counter-Strike 2," "Source 2,"
 * "Steam," and any Game MODs or server software running on software by
 * Valve Corporation. You must obey the GNU General Public License in all
 * respects for all other code used.
 *
 * Additionally, this exception applies to all derivative works unless
 * otherwise stated in LICENSE.txt.
 *
 * Authors:
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */

/*
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
