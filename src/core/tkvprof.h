/**
* vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * Copyright (C) 2025-2026 Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl.
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
 * As a special exception, Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
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
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */
// What Source2Mod core costs, as seen by the engine's profiler (vprof_on,
// then vprof_generate_report / vprof_generate_report_budget).
//
// Not named vprof.h: tier0's own is reachable as <vprof.h> as well.
#pragma once

#include "tier0/vprof.h"

// Budget group every scope of the core is filed under: one "Source2Mod"
// line in the budget report, next to the game's own groups. A plugin's own
// time spent inside a callback the core dispatches lands under the core's
// scope unless the plugin opens a scope of its own.
#define TK_VPROF_BUDGETGROUP "Source2Mod"

// Scope covering the rest of the enclosing block; goes first in a hook
// handler. The name is the node in the report, "Source2Mod::<hooked
// function>", kept under 52 characters: the report cuts the Scope column
// there. A Pre and a Post handler are two scopes, so the game's own call
// between them is not counted.
// VPROF_BUDGET and not VPROF: that one is detail level 1, which the SDK's
// default VPROF_LEVEL 0 compiles out. While vprof is off, or off the main
// thread, tier0 returns right at the top of the enter call.
#define TK_VPROF(name) VPROF_BUDGET(name, TK_VPROF_BUDGETGROUP)
