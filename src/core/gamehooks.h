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

#include <source2mod/ISource2ModHooks.h>

#include <cstddef>
#include <memory>
#include <vector>

// The typed game-function points of ISource2ModHooks (HookPointId::TakeDamage
// .. CheckJumpButtonModern): one detour per function, placed the first time a
// handler is added and kept until shutdown. KHook puts a hook on a function it
// detoured before in from a worker thread, a few milliseconds later; one taken
// out with its last handler and put back for the next would miss the calls in
// between. Without handlers a detour only checks an empty list.
// hooks.cpp hands them their handlers.
namespace gamehooks
{
    /// HookPointId::TakeDamage .. CheckJumpButtonModern.
    constexpr size_t kGameHookCount = static_cast<size_t>(HookPointId::CheckJumpButtonModern) + 1;

    class IGameHook
    {
    public:
        virtual ~IGameHook() = default;

        virtual const char* Name() const = 0;
        virtual bool IsAvailable() = 0;
        /// @p handler is the point's HookHandler<CONTEXT>*.
        virtual Source2ModHookId Add(PluginId owner, const void* handler, bool post, const HookTarget& target, const HookFilter& filter) = 0;
        virtual PluginId OwnerOf(const void* handler) const = 0;
        virtual bool Remove(Source2ModHookId id) = 0;
        virtual void RemoveAll(PluginId owner) = 0;
        virtual void RemoveInstance(const void* instance) = 0;
        virtual bool Empty() const = 0;
        virtual void Uninstall() = 0;
    };

    class GameHooksManager final
    {
    public:
        GameHooksManager();
        ~GameHooksManager();

        Source2ModHookId Add(HookPointId point, PluginId owner, const void* handler, bool post,
                             const HookTarget& target, const HookFilter& filter);
        PluginId OwnerOfHandler(HookPointId point, const void* handler) const;
        bool Remove(Source2ModHookId id);
        bool IsAvailable(HookPointId point);

        void Shutdown();
        void RemoveAllForPlugin(PluginId id);
        void RemoveInstance(const void* instance);

    private:
        std::vector<std::unique_ptr<IGameHook>> m_hooks; // indexed by HookPointId
    };

    extern GameHooksManager gameHooksManager;
}
