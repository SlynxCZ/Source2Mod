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
#include "shared.h"
#include "virtualhooks.h"

#include <igameevents.h>
#include <source2mod/ISource2ModEvents.h>
#include <source2mod/ISource2ModPlugin.h>

#include <functional>

class IGameEvent;

namespace events {
    struct EventEntry
    {
        PluginId owner;
        GameEventHandler handler;
        bool post;
        Source2ModHookId id;
    };

    class EventListener : public IGameEventListener2
    {
        void FireGameEvent(IGameEvent* pEvent) override;
    };

    class EventManager : public ISource2ModEvents
    {
    public:

        Source2ModEventHookId HookGameEvent(const char* pchName, GameEventHandler handler, bool post) override { const PluginId owner = hookid::OwnerOfHandler(handler); return HookGameEvent(owner, pchName, std::move(handler), post); }
        bool UnhookGameEvent(const char* pchName, const GameEventHandler& handler, bool post) override { return UnhookGameEvent(hookid::OwnerOfHandler(handler), pchName, handler, post); }
        bool UnhookGameEvent(Source2ModEventHookId id) override { return Unhook(id); }

        // The same with the owner spelled out: what the calls above resolve
        // to, and what the core itself calls.
        Source2ModEventHookId HookGameEvent(PluginId owner, const char* pchName, GameEventHandler handler, bool post);
        bool UnhookGameEvent(PluginId owner, const char* pchName, const GameEventHandler& handler, bool post);
        bool Unhook(Source2ModHookId id);
    public:
        void RemoveAllForPlugin(PluginId id);
    };

    extern EventManager eventManager;

    void InitEvents();
    void DestructEvents();

    bool DispatchGameEvent(IGameEvent *event, bool post, bool &dontBroadcast);
}
