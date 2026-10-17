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

#include <engine2/isource2server.h>
#include <engine2/ivengineserver2.h>
#include <entitysystem.h>
#include <source2mod/ISource2ModEntities.h>
#include <source2mod/schema/entityio.h>

#include "hookid.h"

#include <string>
#include <vector>

namespace entities {
    class EntitiesManager : public ISource2ModEntities {
    public:
        CCSGameRules* GetGameRules() override;
        CBaseEntity* FindPickerEntity(CBasePlayerController* pPlayer, CCSGameRules* pGameRules = nullptr) override;
        CBaseEntity* FindEntityByClassname(CEntityInstance* pStart, const char* name) override;
        CBaseEntity* FindEntityByName(CEntityInstance* pStartEntity, const char* szName, CEntityInstance* pSearchingEntity = nullptr, CEntityInstance* pActivator = nullptr, CEntityInstance* pCaller = nullptr, IEntityFindFilter* pFilter = nullptr) override;

        std::vector<CBaseEntity*> FindAllEntitiesByDesignerName(const char* pszDesignerName) override;

        CBaseEntity* CreateEntityByName(const char* pszClassName) override;

        void AcceptInput(CEntityInstance* pTarget, const char* pszInput, CEntityInstance* pActivator, CEntityInstance* pCaller, const char* pszValue = "") override;
        void AddEntityIOEvent(CEntityInstance* pTarget, const char* pszInput, CEntityInstance* pActivator = nullptr, CEntityInstance* pCaller = nullptr, const char* pszValue = "", float flDelay = 0.0f) override;

        // The listeners belong to the plugin whose class they are (read off the vtable).
        void AddEntityListener(IEntityListener* pListener, const char* pszDesignerName = nullptr, CEntityInstance* pEntity = nullptr) override
        {
            AddEntityListener(pListener ? hookid::OwnerOf(*reinterpret_cast<void* const*>(pListener)) : 0, pListener, pszDesignerName, pEntity);
        }
        void AddEntityListener(PluginId owner, IEntityListener* pListener, const char* pszDesignerName, CEntityInstance* pEntity);
        void RemoveEntityListener(IEntityListener* pListener) override;

        void AddEntityOutputListener(IEntityOutputListener* pListener, const char* pszDesignerName = nullptr, const char* pszOutput = nullptr, CEntityInstance* pEntity = nullptr) override
        {
            AddEntityOutputListener(pListener ? hookid::OwnerOf(*reinterpret_cast<void* const*>(pListener)) : 0, pListener, pszDesignerName, pszOutput, pEntity);
        }
        void AddEntityOutputListener(PluginId owner, IEntityOutputListener* pListener, const char* pszDesignerName, const char* pszOutput, CEntityInstance* pEntity);
        void RemoveEntityOutputListener(IEntityOutputListener* pListener) override;

        void RemoveAllForPlugin(PluginId id);

        // From the core's own entity listener and FireOutputInternal.
        void NotifyCreated(CEntityInstance* pEntity);
        void NotifySpawned(CEntityInstance* pEntity);
        void NotifyDeleted(CEntityInstance* pEntity);
        void NotifyParentChanged(CEntityInstance* pEntity, CEntityInstance* pNewParent);
        bool HasOutputListeners() const { return !m_OutputListeners.empty(); }
        Action NotifyOutput(CEntityInstance* pCaller, const char* pszOutput, CEntityInstance* pActivator, float flDelay, bool post);

    private:
        struct EntityListenerEntry
        {
            PluginId owner;
            IEntityListener* listener;
            std::string designerName;
            CEntityInstance* entity;
            bool removed = false;
        };

        struct OutputListenerEntry
        {
            PluginId owner;
            IEntityOutputListener* listener;
            std::string designerName;
            std::string output;
            CEntityInstance* entity;
            bool removed = false;
        };

        template <typename FN>
        void ForEachEntityListener(CEntityInstance* pEntity, FN&& fn);
        void Compact();

        // Entries are appended during a dispatch and marked removed, then
        // erased once no dispatch is running: a callback may add or remove
        // listeners (its own too) without invalidating the walk.
        std::vector<EntityListenerEntry> m_EntityListeners;
        std::vector<OutputListenerEntry> m_OutputListeners;
        int m_nDispatchDepth = 0;
    };

    extern EntitiesManager entitiesManager;
}
