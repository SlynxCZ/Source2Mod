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
 * Player helpers (ISource2ModClients). See clients.h.
 */
#include "clients.h"

#include "shared.h"

#include <interfaces/interfaces.h>
#include <inetchannelinfo.h>
#include <source2mod/schema/entity/classes/CCSPlayerController.h>

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace clients
{
    ClientsManager clientsManager;

    namespace
    {
        constexpr int kMaxSlots = 64;
        constexpr uint64_t kSteamId64Base = 76561197960265728ull;

        bool SlotInRange(CPlayerSlot slot)
        {
            return slot.Get() >= 0 && slot.Get() < kMaxSlots;
        }
    }

    CCSPlayerController* ClientsManager::Connected(CPlayerSlot slot)
    {
        if (!SlotInRange(slot))
            return nullptr;
        CCSPlayerController* player = CCSPlayerController::FromSlot(slot);
        if (!player)
            return nullptr;
        const PlayerConnectedState state = player->m_iConnected();
        if (state != PlayerConnectedState::Connected && state != PlayerConnectedState::Connecting
            && state != PlayerConnectedState::Reconnecting)
            return nullptr;
        return player;
    }

    bool ClientsManager::IsConnected(CPlayerSlot slot)
    {
        return Connected(slot) != nullptr;
    }

    bool ClientsManager::IsInGame(CPlayerSlot slot)
    {
        CCSPlayerController* player = Connected(slot);
        return player && player->m_iConnected() == PlayerConnectedState::Connected;
    }

    bool ClientsManager::IsFakeClient(CPlayerSlot slot)
    {
        CCSPlayerController* player = Connected(slot);
        return player && player->IsBot();
    }

    bool ClientsManager::IsAuthorized(CPlayerSlot slot)
    {
        CCSPlayerController* player = Connected(slot);
        return player && !player->IsBot() && g_pEngineServer && g_pEngineServer->IsClientFullyAuthenticated(slot);
    }

    std::string ClientsManager::FormatSteamId(uint64_t steamId, ESource2ModAuthId form)
    {
        if (!steamId)
            return {};
        const uint32_t account = static_cast<uint32_t>(steamId & 0xFFFFFFFFu);
        char buf[64];
        switch (form)
        {
        case SOURCE2MOD_AUTHID_STEAM3:
            std::snprintf(buf, sizeof(buf), "[U:1:%u]", account);
            break;
        case SOURCE2MOD_AUTHID_STEAMID64:
            std::snprintf(buf, sizeof(buf), "%" PRIu64, steamId);
            break;
        default: // the engine form is Steam2, as SourceMod has it on CS:GO
            std::snprintf(buf, sizeof(buf), "STEAM_1:%u:%u", account & 1u, account >> 1);
            break;
        }
        return buf;
    }

    uint64_t ClientsManager::ParseSteamId(const char* text)
    {
        if (!text || !*text)
            return 0;
        unsigned int universe, y, z;
        char tail;
        if (std::sscanf(text, "STEAM_%u:%u:%u%c", &universe, &y, &z, &tail) == 3 && y <= 1)
            return kSteamId64Base + (static_cast<uint64_t>(z) << 1) + y;
        unsigned int account;
        if (std::sscanf(text, "[U:1:%u]%c", &account, &tail) == 1)
            return kSteamId64Base + account;
        if (std::strlen(text) == 17 && std::strncmp(text, "7656", 4) == 0)
        {
            char* end = nullptr;
            const uint64_t id = std::strtoull(text, &end, 10);
            if (end && !*end && id > kSteamId64Base)
                return id;
        }
        return 0;
    }

    std::string ClientsManager::GetAuthId(CPlayerSlot slot, ESource2ModAuthId form, bool validated)
    {
        CCSPlayerController* player = Connected(slot);
        if (!player || player->IsBot())
            return {};
        if (validated && !IsAuthorized(slot))
            return {};
        uint64_t steamId = player->m_steamID();
        if (g_pEngineServer)
        {
            if (const CSteamID* id = g_pEngineServer->GetClientSteamID(slot))
                steamId = id->ConvertToUint64();
        }
        return FormatSteamId(steamId, form);
    }

    float ClientsManager::GetLatency(CPlayerSlot slot)
    {
        CCSPlayerController* player = Connected(slot);
        if (!player || player->IsBot() || !g_pEngineServer)
            return 0.0f;
        INetChannelInfo* info = g_pEngineServer->GetPlayerNetInfo(slot);
        return info ? info->GetAvgLatency() : 0.0f;
    }

    float ClientsManager::GetConnectionTime(CPlayerSlot slot)
    {
        CCSPlayerController* player = Connected(slot);
        if (!player || player->IsBot() || !g_pEngineServer)
            return 0.0f;
        INetChannelInfo* info = g_pEngineServer->GetPlayerNetInfo(slot);
        return info ? info->GetTimeConnected() : 0.0f;
    }

    std::string ClientsManager::GetClientConVar(CPlayerSlot slot, const char* name)
    {
        if (!Connected(slot) || !g_pEngineServer || !name)
            return {};
        const char* value = g_pEngineServer->GetClientConVarValue(slot, name);
        return value ? value : "";
    }

    void ClientsManager::Kick(CPlayerSlot slot, const char* message)
    {
        if (!Connected(slot) || !g_pEngineServer)
            return;
        // CS2 shows the client the reason code's own text; the message is the
        // internal reason the server logs.
        g_pEngineServer->DisconnectClient(slot, NETWORK_DISCONNECT_KICKED, message ? message : "");
    }

    int ClientsManager::GetClientCount(bool inGameOnly)
    {
        int count = 0;
        for (int i = 0; i < kMaxSlots; i++)
        {
            if (inGameOnly ? IsInGame(CPlayerSlot(i)) : IsConnected(CPlayerSlot(i)))
                count++;
        }
        return count;
    }
}
