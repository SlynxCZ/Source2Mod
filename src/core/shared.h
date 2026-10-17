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
#include "coreconfig.h"
#include "gameconfig.h"

#include <engine2/igameeventsystem.h>
#include <engine2/isource2server.h>
#include <engine2/ivengineserver2.h>
#include <entitysystem.h>
#include <gameentitysystem.h>
#include <icvar.h>
#include <igameevents.h>
#include <schemasystem.h>

#include <memory>
#include <vector>

class CCSGameRules;

namespace shared
{
    // Only what interfaces/interfaces.h cannot give us. Everything else --
    // g_pCVar, g_pEngineServer, g_pNetworkMessages, g_pNetworkServerService,
    // g_pSource2Server, g_pSource2GameClients, g_pSource2GameEntities and the
    // rest -- is declared there and filled in by plugin.cpp's GET_V_IFACE_*.
    //
    // IGameEventSystem has no global in interfaces.h at all; CSchemaSystem is the concrete type we need the methods of, not
    // the ISchemaSystem/IGameResourceService the SDK declares; the entity
    // system and event manager are derived at runtime rather than fetched.
    extern IGameEventSystem* g_pGameEventSystem;
    extern CSchemaSystem* g_pSchemaSystem;
    extern CGameEntitySystem* g_pEntitySystem;
    extern IGameEventManager2* g_pGameEventManager;

    extern CGlobalVars* g_pGlobalVars;
    extern CCoreConfig* g_pCoreConfig;
    extern CGameConfig* g_pGameConfig;
    extern CCSGameRules* g_pGameRules;

    CGlobalVars* getGlobalVars();
}
