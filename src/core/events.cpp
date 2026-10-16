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
#include "events.h"

#include "hookid.h"
#include "shared.h"
#include "slowguard.h"
#include "utils/log.h"
#include "virtualhooks.h"

namespace events {
    static std::unordered_map<std::string, std::vector<EventEntry> > gameEvents;
    static EventListener eventListener;

    EventManager eventManager;

    static void RegisterListenerIfNeeded(const std::string& name)
    {
        if (!shared::g_pGameEventManager)
            return;

        if (!shared::g_pGameEventManager->FindListener(&eventListener, name.c_str()))
        {
            shared::g_pGameEventManager->AddListener(&eventListener, name.c_str(), true);
        }
    }

    Source2ModEventHookId EventManager::HookGameEvent(PluginId owner, const char* name, GameEventHandler handler, bool post)
    {
        // 0 for an event the game does not have: a listener on a name that
        // never fires would only sit there.
        if (!name || !*name || (shared::g_pGameEventManager && !shared::g_pGameEventManager->HasEventDescriptor(name)))
            return 0;

        const Source2ModHookId id = hookid::Next();
        gameEvents[name].push_back({ owner, std::move(handler), post, id });
        RegisterListenerIfNeeded(name);
        return id;
    }

    bool EventManager::UnhookGameEvent(PluginId owner, const char* name, const GameEventHandler& handler, bool post)
    {
        if (!handler.HasIdentity())
            return false;

        auto it = gameEvents.find(name);
        if (it == gameEvents.end())
            return false;

        const bool found = std::erase_if(it->second, [owner, post, &handler](const EventEntry& e)
        {
            return e.owner == owner && e.post == post && handler.SameAs(e.handler);
        }) > 0;

        if (it->second.empty())
            gameEvents.erase(it);
        return found;
    }

    bool EventManager::Unhook(Source2ModHookId id)
    {
        for (auto it = gameEvents.begin(); it != gameEvents.end(); ++it)
        {
            if (std::erase_if(it->second, [id](const EventEntry& e) { return e.id == id; }) > 0)
            {
                if (it->second.empty())
                    gameEvents.erase(it);
                return true;
            }
        }
        return false;
    }

    void EventManager::RemoveAllForPlugin(PluginId id)
    {
        for (auto it = gameEvents.begin(); it != gameEvents.end(); )
        {
            auto& vec = it->second;

            std::erase_if(vec, [id](const EventEntry& e) {
                return e.owner == id;
            });

            if (vec.empty())
                it = gameEvents.erase(it);
            else
                ++it;
        }
    }

    void EventListener::FireGameEvent(IGameEvent* pEvent) {}

    void InitEvents()
    {
        if (!shared::g_pGameEventManager)
            return;

        for (const auto& [name, _] : gameEvents)
        {
            RegisterListenerIfNeeded(name);
        }
    }

    void DestructEvents()
    {
        if (shared::g_pGameEventManager)
            shared::g_pGameEventManager->RemoveListener(&eventListener);
        gameEvents.clear();
    }

    bool DispatchGameEvent(IGameEvent *event, bool post, bool &dontBroadcast) {
        const char *name = event->GetName();
        auto it = gameEvents.find(name);
        if (it == gameEvents.end())
            return true;

        for (const auto &hook: it->second) {
            if (hook.post != post)
                continue;

            Action result;
            {
                ::slow::Guard slowGuard("game event handler", name, hook.owner);
                result = hook.handler(event, post, dontBroadcast);
            }

            if (result == Action::Supersede)
                return false;
        }

        return true;
    }
}
