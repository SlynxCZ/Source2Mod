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

#include <engine2/isource2server.h>
#include <engine2/ivengineserver2.h>
#include <entitysystem.h>
#include <source2mod/ISource2ModEntities.h>
#include <source2mod/schema/entityio.h>

#include <vector>

namespace entities {
    class EntitiesManager : public ISource2ModEntities {
    public:
        CCSGameRules* GetGameRules() override;
        CBaseEntity* FindPickerEntity(CBasePlayerController* pPlayer, CCSGameRules* pGameRules = nullptr) override;
        CBaseEntity* FindEntityByClassname(CEntityInstance* pStart, const char* name) override;
        CBaseEntity* FindEntityByName(CEntityInstance* pStartEntity, const char* szName, CEntityInstance* pSearchingEntity = nullptr, CEntityInstance* pActivator = nullptr, CEntityInstance* pCaller = nullptr, IEntityFindFilter* pFilter = nullptr) override;

        CBaseEntity* CreateEntityByName(const char* pszClassName) override;

        // A listener object's vtable lies in the module that defined its class:
        // that is whose it is.
        void AddEntityListener(IEntityListener* pListener) override
        {
            AddEntityListener(pListener ? hookid::OwnerOf(*reinterpret_cast<void* const*>(pListener)) : 0, pListener);
        }
        void AddEntityListener(PluginId owner, IEntityListener* pListener);
        void RemoveEntityListener(IEntityListener* pListener) override;

        void AcceptInput(CEntityInstance* pTarget, const char* pszInput, CEntityInstance* pActivator, CEntityInstance* pCaller, const char* pszValue = "") override;
        void AddEntityIOEvent(CEntityInstance* pTarget, const char* pszInput, CEntityInstance* pActivator = nullptr, CEntityInstance* pCaller = nullptr, const char* pszValue = "", float flDelay = 0.0f) override;

        void AddEntityIOListener(IEntityIOListener* pListener, const char* pchClassName, const char* pchOutputName, bool post = false) override
        {
            AddEntityIOListener(pListener ? hookid::OwnerOf(*reinterpret_cast<void* const*>(pListener)) : 0, pListener, pchClassName, pchOutputName, post);
        }
        void AddEntityIOListener(PluginId owner, IEntityIOListener* pListener, const char* pchClassName, const char* pchOutputName, bool post = false);
        void RemoveEntityIOListener(IEntityIOListener* pListener, const char* pchClassName, const char* pchOutputName, bool post = false) override;

    public:
        /// Attaches every registered listener to the current entity system.
        /// Called on StartupServer, including when the engine has made a new
        /// one for the next map.
        void AttachEntityListeners();

        /// Takes this plugin's listeners off the entity system and out of the
        /// entity I/O stack. Both hold the listener object itself, which lives
        /// inside the plugin's library, so it has to go before the library is
        /// closed -- the engine would otherwise keep calling a vtable that is
        /// no longer mapped.
        void RemoveAllForPlugin(PluginId id);

    private:
        /// Forgets who registered an I/O listener, once it is off every output
        /// it was watching.
        void DropIOListenerRecord(IEntityIOListener* pListener);

        struct ListenerEntry
        {
            PluginId owner;
            IEntityListener* listener;
        };

        struct IOListenerEntry
        {
            PluginId owner;
            IEntityIOListener* listener;
        };

        // Every listener a plugin has registered, kept for as long as it is
        // registered rather than until it is first attached. A plugin
        // registers while it loads, which is before the engine has made an
        // entity system at all, and the engine may make another one on the
        // next map -- both are handled by re-attaching this list.
        std::vector<ListenerEntry> m_Listeners;

        // The I/O listeners themselves live in inlinehooks::entityIOListenerStack,
        // keyed by the output they watch. This is only who registered what, so
        // a plugin's can be found again when it unloads.
        std::vector<IOListenerEntry> m_IOListeners;
    };

    extern EntitiesManager entitiesManager;
}
