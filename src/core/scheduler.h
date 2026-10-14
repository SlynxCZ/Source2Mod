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
#pragma once

#include "hookid.h"

#include <source2mod/ISource2ModScheduler.h>

#include <functional>

extern bool g_bHasTicked;
extern double g_dUniversalTime;
/// The curtime of the last simulated frame (0 before the first).
extern double g_dLastTickTime;
extern double g_dTimerNextThink;

namespace scheduler
{
    void Init();
    void Shutdown();
    void Tick(bool simulating = true);
    void RemoveMapChangeTimers();

    class Scheduler : public ISource2ModScheduler
    {
    public:
        void NextFrame(TimerCallback task) override
        {
            const PluginId owner = hookid::OwnerOfHandler(task);
            NextFrame(owner, std::function<void()>([task = std::move(task)]() { task(); }));
        }
        Timer* AddTimer(float interval, TimerCallback callback, int flags = 0) override
        {
            const PluginId owner = hookid::OwnerOfHandler(callback);
            return AddTimer(owner, interval, std::move(callback), flags);
        }

        // The same with the owner spelled out: what the calls above resolve
        // to, and what the core itself calls.
        void NextFrame(PluginId owner, std::function<void()>&& task);
        Timer* AddTimer(PluginId owner, float interval, TimerCallback callback, int flags = 0);
        void KillTimer(Timer* timer) override;

        /// Drops every timer and pending next-frame task this plugin owns.
        /// Called while its library is still mapped -- see Timer::Owner.
        void RemoveAllForPlugin(PluginId id);
    };

    extern Scheduler schedulerManager;
}
