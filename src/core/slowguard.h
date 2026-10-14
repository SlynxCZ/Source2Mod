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
#pragma once

#include "tier0/platform.h"

/**
 * Slow plugin callbacks, named.
 *
 * Everything a plugin does runs inside a hook of the core, so the engine's
 * frame-time report blames "Source2Mod" for a stall wherever it really
 * is: a command handler waiting on a socket, a timer doing a query, an event
 * handler in a loop. The guard times one callback and logs which one, whose,
 * and how long, when it took longer than SlowCallbackWarnMs (core.json).
 * The cost when nothing is slow is two clock reads.
 */
namespace slow
{
    /// Milliseconds a callback may take before it is logged; 0 turns it off.
    extern double g_flWarnMs;

    /// Logs one slow callback. `what` is the kind ("command handler"), `name`
    /// what it was on ("admin"), `owner` the plugin id (0: the core).
    void Report(const char* what, const char* name, int owner, double ms);

    class Guard
    {
    public:
        Guard(const char* what, const char* name, int owner)
            : m_what(what), m_name(name), m_owner(owner), m_t0(g_flWarnMs > 0.0 ? Plat_FloatTime() : 0.0)
        {
        }

        ~Guard()
        {
            if (g_flWarnMs <= 0.0)
                return;

            const double ms = (Plat_FloatTime() - m_t0) * 1000.0;
            if (ms >= g_flWarnMs)
                Report(m_what, m_name, m_owner, ms);
        }

        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

    private:
        const char* m_what;
        const char* m_name;
        int m_owner;
        double m_t0;
    };
}
