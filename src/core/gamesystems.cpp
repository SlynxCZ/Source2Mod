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
#include "gamesystems.h"

#include "addresses.h"
#include "gameconfig.h"
#include "shared.h"
#include "utils/log.h"

// The engine keeps its game system factories in a singly linked list whose head
// is a static this class declares but never defines -- the definition belongs to
// whichever binary is doing the registering. Source2Mod owns it here, and hands
// the address out through GetFactoryListHead() so a plugin can point its own
// copy at the same list.
CBaseGameSystemFactory** CBaseGameSystemFactory::sm_pFirst = nullptr;

namespace gamesystems
{
    GameSystemsManager gameSystemsManager;

    bool GameSystemsManager::Init()
    {
        if (m_ppFirst)
            return true;

        // The signature lands on the instruction that references sm_pFirst
        // rather than on the variable, so this walks the RIP-relative operand:
        // skip the 3-byte opcode, read the 4-byte displacement, and add it to
        // the address of the next instruction.
        auto& mod = addresses::source2modAddresses.GetOrLoadModule(g_pSource2Server);
        auto addr = mod.FindPattern(
            DynLibUtils::ParsePattern(shared::g_pGameConfig->GetSignature("IGameSystem::InitAllSystems->pFirst")));

        auto* pInstruction = addr.RCast<uint8*>();

        if (!addr || !pInstruction)
        {
            FP_ERROR("Failed to resolve IGameSystem::InitAllSystems_pFirst -- game systems unavailable.");
            return false;
        }

        uint8* pDisplacement = pInstruction + 3;
        const uint32 nOffset = *reinterpret_cast<uint32*>(pDisplacement);
        uint8* pNextInstruction = pDisplacement + 4;

        m_ppFirst = reinterpret_cast<CBaseGameSystemFactory**>(pNextInstruction + nOffset);

        // Point Source2Mod's own copy at it too, so anything the core itself
        // registers lands in the same list.
        CBaseGameSystemFactory::sm_pFirst = m_ppFirst;

        FP_INFO("Game system factory list resolved.");
        return true;
    }

    bool GameSystemsManager::IsAvailable() const
    {
        return m_ppFirst != nullptr;
    }

    CBaseGameSystemFactory** GameSystemsManager::GetFactoryListHead()
    {
        return m_ppFirst;
    }

    IGameSystem* GameSystemsManager::FindByName(const char* name)
    {
        if (!m_ppFirst || !name)
            return nullptr;

        // The hl2sdk walks the list itself; m_pNext/m_pName are private, and
        // this static reads the very sm_pFirst Init() filled in.
        return CBaseGameSystemFactory::GetGlobalPtrByName(name);
    }
}
