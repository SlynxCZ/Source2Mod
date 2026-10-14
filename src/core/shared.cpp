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
#include "shared.h"

#include <engine2/inetworkserverservice.h>
#include <engine2/networkgameserverbase.h>
#include <icvar.h>
#include <schemasystem.h>

namespace shared
{
    IGameEventSystem* g_pGameEventSystem = nullptr;
    CSchemaSystem* g_pSchemaSystem = nullptr;
    CGameEntitySystem* g_pEntitySystem = nullptr;
    IGameEventManager2* g_pGameEventManager = nullptr;

    CGlobalVars* g_pGlobalVars = nullptr;
    CCoreConfig* g_pCoreConfig = nullptr;
    CGameConfig* g_pGameConfig = nullptr;
    CCSGameRules* g_pGameRules = nullptr;

    CGlobalVars *getGlobalVars() {
        if (!g_pEngineServer) return g_pGlobalVars;
        g_pGlobalVars = g_pEngineServer->GetServerGlobals();
        return g_pGlobalVars;
    }

    bool g_bDetoursLoaded = false;
}
