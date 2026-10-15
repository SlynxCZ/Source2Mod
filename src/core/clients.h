/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * Player helpers the controller classes do not have (ISource2ModClients).
 *
 * Project: Source2Mod
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
