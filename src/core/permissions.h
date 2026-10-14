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

#pragma once
#include "hookid.h"

#include <const.h>
#include <playerslot.h>
#include <source2mod/ISource2ModPermissions.h>
#include <steam/steam_gameserver.h>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace permissions
{
    /// Which SteamID a connected player is checked under (core.json SteamAuthMode).
    enum class SteamAuthMode
    {
        Auto,       // the claimed one when the server does not validate tickets, else the validated one
        Flexible,   // the claimed one right away
        Strict,     // the validated one only
    };

    SteamAuthMode ParseSteamAuthMode(const std::string& mode);

    class PermissionsManager : public ISource2ModPermissions
    {
    public:
        // ISource2ModPermissions
        bool HasPermission(uint64 steamId, const char* permission) override;
        bool PlayerHasPermission(CPlayerSlot slot, const char* permission) override;
        bool CheckAccess(CPlayerSlot slot, const char* name, const char* defaultPermission) override;
        bool InGroup(uint64 steamId, const char* group) override;
        int GetImmunity(uint64 steamId) override;
        bool CanTarget(CPlayerSlot caller, CPlayerSlot target) override;
        uint64 GetPlayerSteamID(CPlayerSlot slot) override;
        bool IsPlayerAuthorized(CPlayerSlot slot) override;

        void GrantPermission(PluginId owner, uint64 steamId, const char* permission) override;
        void RevokePermission(PluginId owner, uint64 steamId, const char* permission) override;
        void AddToGroup(PluginId owner, uint64 steamId, const char* group) override;
        void RemoveFromGroup(PluginId owner, uint64 steamId, const char* group) override;
        void SetImmunity(PluginId owner, uint64 steamId, int immunity) override;
        void ClearPlayer(PluginId owner, uint64 steamId) override;

        void CreateGroup(PluginId owner, const char* group, int immunity) override;
        void DeleteGroup(PluginId owner, const char* group) override;
        void AddGroupPermission(PluginId owner, const char* group, const char* permission) override;
        void RemoveGroupPermission(PluginId owner, const char* group, const char* permission) override;
        void AddGroupParent(PluginId owner, const char* group, const char* parent) override;
        void RemoveGroupParent(PluginId owner, const char* group, const char* parent) override;

        Source2ModHookId SetAccessDeniedHandler(AccessDeniedHandler handler) override;
        bool ClearAccessDeniedHandler(Source2ModHookId id) override;

        // Core side
        void Init();
        void Shutdown();

        /// Re-reads permissions.json into the file's layer. False with `error` when it does not parse.
        bool LoadFile(std::string& error);

        /// The permission a command needs: its "Overrides" entry, else what it was registered with.
        std::string CommandPermission(const std::string& command, const std::string& registered) const;

        /// Tells a refused player so: the plugin handler set last, else the core's line.
        void Deny(const Source2ModCommandContext& ctx, const Source2ModCommandArgs& args, const std::string& permission);

        void OnClientPutInServer(CPlayerSlot slot, int type, uint64 xuid);
        void OnClientDisconnect(CPlayerSlot slot);
        void OnGameFrame();
        void OnSteamAPIActivated();
        void OnSteamAPIDeactivated();

        void RemoveAllForPlugin(PluginId id);

        /// For "s2m admins": one line per connected player, and what one SteamID holds.
        std::vector<std::string> DescribePlayers();
        std::vector<std::string> DescribeSteamID(uint64 steamId);

        /// "76561198...", "STEAM_1:0:123", "[U:1:246]"; 0 when it is none of them.
        static uint64 ParseSteamID(const std::string& text);

    private:
        struct GroupDef
        {
            std::set<std::string> permissions;
            std::set<std::string> parents;
            std::optional<int> immunity;
        };

        struct PlayerDef
        {
            std::set<std::string> permissions;
            std::set<std::string> groups;
            std::optional<int> immunity;
        };

        /// What one source holds: the file (owner 0) or one plugin.
        struct Layer
        {
            std::unordered_map<std::string, GroupDef> groups;
            std::unordered_map<uint64, PlayerDef> players;
        };

        /// What a SteamID ends up with across every layer and group.
        struct Effective
        {
            std::vector<std::string> permissions;
            std::unordered_set<std::string> groups;
            int immunity = 0;
            bool root = false;
        };

        struct SlotState
        {
            bool connected = false;
            bool fake = false;
            bool authorized = false;
            bool refused = false;
            uint64 xuid = 0;        // what the client claimed
            uint64 steamId = 0;     // what it is checked under now
        };

        const Effective& Resolve(uint64 steamId);
        static bool Matches(const std::vector<std::string>& held, const std::string& permission);

        Layer& LayerOf(PluginId owner) { return m_layers[owner]; }
        void Changed(uint64 steamId);
        void ChangedAll();

        bool UsesClaimedSteamID() const;
        void Authorize(int slot);
        void OnValidateAuthTicket(ValidateAuthTicketResponse_t* response);

        SlotState* Slot(CPlayerSlot slot);

        std::map<PluginId, Layer> m_layers;
        std::unordered_map<std::string, std::string> m_overrides;
        std::unordered_map<uint64, Effective> m_cache;
        bool m_bNotifyAll = false;

        SteamAuthMode m_authMode = SteamAuthMode::Auto;
        std::string m_deniedMessage;
        SlotState m_slots[ABSOLUTE_PLAYER_LIMIT];

        hookid::HookList<AccessDeniedHandler> m_deniedHandlers;

        CCallbackManual<PermissionsManager, ValidateAuthTicketResponse_t, true> m_authCallback;
        bool m_bAuthCallback = false;
    };

    extern PermissionsManager permissionsManager;
}
