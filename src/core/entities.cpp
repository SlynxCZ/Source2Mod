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
#include "entities.h"

#include "addresses.h"
#include "commands.h"
#include "events.h"
#include "inlinehooks.h"
#include "shared.h"

#include <source2mod/schema/entity/classes/CBaseEntity.h>
#include <source2mod/schema/entity/classes/CBasePlayerController.h>
#include <source2mod/schema/entity/classes/CCSGameRules.h>
#include <source2mod/schema/entityio.h>
#include <source2mod/utils/plat.h>
#include <source2mod/utils/virtual.h>

#include <algorithm>

namespace entities
{
    EntitiesManager entitiesManager;

    CCSGameRules* EntitiesManager::GetGameRules()
    {
        // Captured from cs_gamerules by the entity listener, so it is whatever
        // the current map has -- null only before that entity exists.
        return shared::g_pGameRules;
    }

    CBaseEntity* EntitiesManager::FindPickerEntity(CBasePlayerController* pPlayer, CCSGameRules* pGameRules)
    {
        static int offset = shared::g_pGameConfig->GetOffset("CGameRules::FindPickerEntity");
        auto* gr = pGameRules ? pGameRules : shared::g_pGameRules;
        return CALL_VIRTUAL(CBaseEntity*, offset, gr, pPlayer ? pPlayer : nullptr, nullptr);
    }

    CBaseEntity* EntitiesManager::FindEntityByClassname(CEntityInstance* pStart, const char* name)
    {
        // Nothing to search before the first StartupServer.
        if (!shared::g_pEntitySystem)
            return nullptr;

        return addresses::source2modAddresses.CGameEntitySystem_FindEntityByClassName()(shared::g_pEntitySystem, pStart, name);
    }

    CBaseEntity* EntitiesManager::FindEntityByName(CEntityInstance* pStartEntity, const char* szName, CEntityInstance* pSearchingEntity, CEntityInstance* pActivator, CEntityInstance* pCaller, IEntityFindFilter* pFilter)
    {
        if (!shared::g_pEntitySystem)
            return nullptr;

        return addresses::source2modAddresses.CGameEntitySystem_FindEntityByName()(shared::g_pEntitySystem, pStartEntity, szName, pSearchingEntity, pActivator, pCaller, pFilter);
    }

    CBaseEntity* EntitiesManager::CreateEntityByName(const char* pszClassName)
    {
        return addresses::source2modAddresses.CBaseEntity_CreateEntityByName()(pszClassName, -1);
    }

    void EntitiesManager::AddEntityListener(PluginId owner, IEntityListener* pListener)
    {
        if (!pListener)
            return;

        auto known = std::find_if(m_Listeners.begin(), m_Listeners.end(),
                                  [pListener](const ListenerEntry& e) { return e.listener == pListener; });

        if (known != m_Listeners.end())
            return;

        m_Listeners.push_back(ListenerEntry{ owner, pListener });

        // Null while a plugin is still loading -- the engine has not made the
        // entity system yet. AttachEntityListeners() picks it up on
        // StartupServer.
        if (shared::g_pEntitySystem)
            shared::g_pEntitySystem->AddListenerEntity(pListener);
    }

    void EntitiesManager::RemoveEntityListener(IEntityListener* pListener)
    {
        if (!pListener)
            return;

        std::erase_if(m_Listeners, [pListener](const ListenerEntry& e) { return e.listener == pListener; });

        if (shared::g_pEntitySystem)
            shared::g_pEntitySystem->RemoveListenerEntity(pListener);
    }

    void EntitiesManager::AttachEntityListeners()
    {
        if (!shared::g_pEntitySystem)
            return;

        // The list is kept, not consumed: a new map can mean a new entity
        // system, and every listener has to be put on that one too.
        for (const ListenerEntry& entry : m_Listeners)
            shared::g_pEntitySystem->AddListenerEntity(entry.listener);
    }

    void EntitiesManager::AcceptInput(CEntityInstance* pTarget, const char* pszInput, CEntityInstance* pActivator, CEntityInstance* pCaller, const char* pszValue)
    {
        addresses::source2modAddresses.CEntityInstance_AcceptInput()(pTarget, pszInput, pActivator, pCaller, variant_t(pszValue));
    }

    void EntitiesManager::AddEntityIOEvent(CEntityInstance* pTarget, const char* pszInput, CEntityInstance* pActivator, CEntityInstance* pCaller, const char* pszValue, float flDelay)
    {
        if (!shared::g_pEntitySystem)
            return;

        addresses::source2modAddresses.CEntitySystem_AddEntityIOEvent()(shared::g_pEntitySystem, pTarget, pszInput, pActivator, pCaller, variant_t(pszValue), flDelay, nullptr, nullptr);
    }

    void EntitiesManager::AddEntityIOListener(PluginId owner, IEntityIOListener* pListener, const char* pchClassName, const char* pchOutputName, bool post)
    {
        OutputKey key{
            pchClassName ? pchClassName : "*",
            pchOutputName ? pchOutputName : "*"
        };

        if (post)
            inlinehooks::entityIOListenerStack[key].m_vecPost.push_back(pListener);
        else
            inlinehooks::entityIOListenerStack[key].m_vecPre.push_back(pListener);

        m_IOListeners.push_back(IOListenerEntry{ owner, pListener });
    }

    void EntitiesManager::RemoveEntityIOListener(IEntityIOListener* pListener, const char* pchClassName, const char* pchOutputName, bool post)
    {
        if (!pchClassName && !pchOutputName)
        {
            for (auto it = inlinehooks::entityIOListenerStack.begin(); it != inlinehooks::entityIOListenerStack.end(); )
            {
                auto& vec = post ? it->second.m_vecPost : it->second.m_vecPre;

                std::erase(vec, pListener);

                if (it->second.m_vecPre.empty() && it->second.m_vecPost.empty())
                    it = inlinehooks::entityIOListenerStack.erase(it);
                else
                    ++it;
            }

            DropIOListenerRecord(pListener);
            return;
        }

        OutputKey key{
            pchClassName ? pchClassName : "*",
            pchOutputName ? pchOutputName : "*"
        };

        auto it = inlinehooks::entityIOListenerStack.find(key);
        if (it == inlinehooks::entityIOListenerStack.end())
            return;

        auto& vec = post ? it->second.m_vecPost : it->second.m_vecPre;

        std::erase(vec, pListener);

        if (it->second.m_vecPre.empty() && it->second.m_vecPost.empty())
            inlinehooks::entityIOListenerStack.erase(it);

        DropIOListenerRecord(pListener);
    }

    void EntitiesManager::DropIOListenerRecord(IEntityIOListener* pListener)
    {
        // Only once the listener is gone from every output it watched -- the
        // same object can be registered for several, and the record is what
        // says a plugin still has one.
        for (const auto& [key, pair] : inlinehooks::entityIOListenerStack)
        {
            if (std::find(pair.m_vecPre.begin(), pair.m_vecPre.end(), pListener) != pair.m_vecPre.end())
                return;

            if (std::find(pair.m_vecPost.begin(), pair.m_vecPost.end(), pListener) != pair.m_vecPost.end())
                return;
        }

        std::erase_if(m_IOListeners, [pListener](const IOListenerEntry& e) { return e.listener == pListener; });
    }

    void EntitiesManager::RemoveAllForPlugin(PluginId id)
    {
        for (auto it = m_Listeners.begin(); it != m_Listeners.end(); )
        {
            if (it->owner != id)
            {
                ++it;
                continue;
            }

            if (shared::g_pEntitySystem)
                shared::g_pEntitySystem->RemoveListenerEntity(it->listener);

            it = m_Listeners.erase(it);
        }

        std::vector<IEntityIOListener*> going;

        for (const auto& entry : m_IOListeners)
        {
            if (entry.owner == id)
                going.push_back(entry.listener);
        }

        for (auto* pListener : going)
        {
            RemoveEntityIOListener(pListener, nullptr, nullptr, false);
            RemoveEntityIOListener(pListener, nullptr, nullptr, true);
        }

        // Anything the two passes above could not reach, so a plugin never
        // leaves a record behind pointing into its closed library.
        std::erase_if(m_IOListeners, [id](const IOListenerEntry& e) { return e.owner == id; });
    }
}
