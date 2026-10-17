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
#include <cstring>

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

    void EntitiesManager::AcceptInput(CEntityInstance* pTarget, const char* pszInput, CEntityInstance* pActivator, CEntityInstance* pCaller, const char* pszValue)
    {
        addresses::source2modAddresses.CEntityInstance_AcceptInput()(pTarget, pszInput, pActivator, pCaller, variant_t(pszValue));
    }

    std::vector<CBaseEntity*> EntitiesManager::FindAllEntitiesByDesignerName(const char* pszDesignerName)
    {
        std::vector<CBaseEntity*> results;
        CGameEntitySystem* pEntitySystem = shared::g_pEntitySystem;
        if (!pszDesignerName || !pEntitySystem)
            return results;

        for (CEntityIdentity* pIdentity = pEntitySystem->m_EntityList.m_pFirstActiveEntity; pIdentity; pIdentity = pIdentity->m_pNext)
        {
            const char* pszDesigner = pIdentity->m_pInstance ? pIdentity->m_designerName.String() : nullptr;
            if (pszDesigner && std::strcmp(pszDesigner, pszDesignerName) == 0)
                results.push_back(static_cast<CBaseEntity*>(pIdentity->m_pInstance));
        }
        return results;
    }

    void EntitiesManager::AddEntityIOEvent(CEntityInstance* pTarget, const char* pszInput, CEntityInstance* pActivator, CEntityInstance* pCaller, const char* pszValue, float flDelay)
    {
        if (!shared::g_pEntitySystem)
            return;

        addresses::source2modAddresses.CEntitySystem_AddEntityIOEvent()(shared::g_pEntitySystem, pTarget, pszInput, pActivator, pCaller, variant_t(pszValue), flDelay, nullptr, nullptr);
    }

    namespace
    {
        bool DesignerMatches(const std::string& wanted, CEntityInstance* pEntity)
        {
            if (wanted.empty())
                return true;
            const char* designer = pEntity ? pEntity->GetClassname() : nullptr;
            return designer && wanted == designer;
        }
    }

    void EntitiesManager::AddEntityListener(PluginId owner, IEntityListener* pListener, const char* pszDesignerName, CEntityInstance* pEntity)
    {
        if (!pListener)
            return;
        m_EntityListeners.push_back(EntityListenerEntry{ owner, pListener, pszDesignerName ? pszDesignerName : "", pEntity });
    }

    void EntitiesManager::RemoveEntityListener(IEntityListener* pListener)
    {
        for (auto& e : m_EntityListeners)
            if (e.listener == pListener)
                e.removed = true;
        Compact();
    }

    void EntitiesManager::AddEntityOutputListener(PluginId owner, IEntityOutputListener* pListener, const char* pszDesignerName, const char* pszOutput, CEntityInstance* pEntity)
    {
        if (!pListener)
            return;
        m_OutputListeners.push_back(OutputListenerEntry{ owner, pListener, pszDesignerName ? pszDesignerName : "", pszOutput ? pszOutput : "", pEntity });
    }

    void EntitiesManager::RemoveEntityOutputListener(IEntityOutputListener* pListener)
    {
        for (auto& e : m_OutputListeners)
            if (e.listener == pListener)
                e.removed = true;
        Compact();
    }

    void EntitiesManager::RemoveAllForPlugin(PluginId id)
    {
        for (auto& e : m_EntityListeners)
            if (e.owner == id)
                e.removed = true;
        for (auto& e : m_OutputListeners)
            if (e.owner == id)
                e.removed = true;
        Compact();
    }

    void EntitiesManager::Compact()
    {
        if (m_nDispatchDepth > 0)
            return;
        std::erase_if(m_EntityListeners, [](const EntityListenerEntry& e) { return e.removed; });
        std::erase_if(m_OutputListeners, [](const OutputListenerEntry& e) { return e.removed; });
    }

    template <typename FN>
    void EntitiesManager::ForEachEntityListener(CEntityInstance* pEntity, FN&& fn)
    {
        if (m_EntityListeners.empty())
            return;

        ++m_nDispatchDepth;
        // By index and up to the count at the start: one added meanwhile waits
        // for the next event, and a push_back may move the vector.
        const size_t count = m_EntityListeners.size();
        for (size_t i = 0; i < count; ++i)
        {
            const EntityListenerEntry& e = m_EntityListeners[i];
            if (e.removed || (e.entity && e.entity != pEntity) || !DesignerMatches(e.designerName, pEntity))
                continue;
            fn(e.listener);
        }
        --m_nDispatchDepth;
        Compact();
    }

    void EntitiesManager::NotifyCreated(CEntityInstance* pEntity)
    {
        ForEachEntityListener(pEntity, [pEntity](IEntityListener* l) { l->OnEntityCreated(pEntity); });
    }

    void EntitiesManager::NotifySpawned(CEntityInstance* pEntity)
    {
        ForEachEntityListener(pEntity, [pEntity](IEntityListener* l) { l->OnEntitySpawned(pEntity); });
    }

    void EntitiesManager::NotifyDeleted(CEntityInstance* pEntity)
    {
        ForEachEntityListener(pEntity, [pEntity](IEntityListener* l) { l->OnEntityDeleted(pEntity); });

        // A registration for this one entity goes with it: the pointer may
        // be another entity's next.
        for (auto& e : m_EntityListeners)
            if (e.entity == pEntity)
                e.removed = true;
        for (auto& e : m_OutputListeners)
            if (e.entity == pEntity)
                e.removed = true;
        Compact();
    }

    void EntitiesManager::NotifyParentChanged(CEntityInstance* pEntity, CEntityInstance* pNewParent)
    {
        ForEachEntityListener(pEntity, [pEntity, pNewParent](IEntityListener* l) { l->OnEntityParentChanged(pEntity, pNewParent); });
    }

    Action EntitiesManager::NotifyOutput(CEntityInstance* pCaller, const char* pszOutput, CEntityInstance* pActivator, float flDelay, bool post)
    {
        Action strongest = Action::Ignore;
        if (m_OutputListeners.empty())
            return strongest;

        ++m_nDispatchDepth;
        const size_t count = m_OutputListeners.size();
        for (size_t i = 0; i < count; ++i)
        {
            const OutputListenerEntry& e = m_OutputListeners[i];
            if (e.removed || (e.entity && e.entity != pCaller) || !DesignerMatches(e.designerName, pCaller))
                continue;
            if (!e.output.empty() && (!pszOutput || e.output != pszOutput))
                continue;

            const Action action = e.listener->OnEntityOutput(pCaller, pszOutput, pActivator, flDelay, post);
            if (static_cast<int>(action) > static_cast<int>(strongest))
                strongest = action;
        }
        --m_nDispatchDepth;
        Compact();

        return strongest;
    }
}
