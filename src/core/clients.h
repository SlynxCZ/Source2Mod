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

/*
 * Player helpers the controller classes do not have (ISource2ModClients).
 */
#pragma once

#include <source2mod/ISource2ModClients.h>

#include <cstdint>
#include <string>

class CCSPlayerController;

namespace clients
{
    class ClientsManager final : public ISource2ModClients
    {
    public:
        bool IsConnected(CPlayerSlot slot) override;
        bool IsInGame(CPlayerSlot slot) override;
        bool IsFakeClient(CPlayerSlot slot) override;
        bool IsAuthorized(CPlayerSlot slot) override;
        std::string GetAuthId(CPlayerSlot slot, ESource2ModAuthId form, bool validated = true) override;
        float GetLatency(CPlayerSlot slot) override;
        float GetConnectionTime(CPlayerSlot slot) override;
        std::string GetClientConVar(CPlayerSlot slot, const char* name) override;
        void Kick(CPlayerSlot slot, const char* message) override;
        int GetClientCount(bool inGameOnly = true) override;

        // ---- core side ----

        /// The controller of a connected (connecting or in game) slot; nullptr otherwise.
        static CCSPlayerController* Connected(CPlayerSlot slot);
        /// A SteamID64 in a form; empty for 0.
        static std::string FormatSteamId(uint64_t steamId, ESource2ModAuthId form);
        /// Any SteamID form back to a SteamID64; 0 when the text is none.
        static uint64_t ParseSteamId(const char* text);
    };

    extern ClientsManager clientsManager;
}
