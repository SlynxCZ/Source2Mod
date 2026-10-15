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
#include "permissions.h"

#include "commands.h"
#include "cookies.h"
#include "pluginmanager.h"
#include "shared.h"
#include "utils/log.h"
#include "utils/paths.h"

#include <nlohmann/json.hpp>
#include <tier0/convar.h>
#include <tier0/icommandline.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace permissions
{
    PermissionsManager permissionsManager;

    namespace
    {
        // Every SteamID64 of an individual account is this plus the account id.
        constexpr uint64 kSteamID64Base = 76561197960265728ULL;

        // Applies to everybody, a player without a known SteamID included.
        constexpr const char* kDefaultGroup = "default";

        constexpr const char* kDefaultDeniedMessage = "You do not have access to this command.";

        std::string Lower(const char* text)
        {
            std::string out = text ? text : "";
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }

        bool IsValidSlot(int slot)
        {
            return slot >= 0 && slot < ABSOLUTE_PLAYER_LIMIT;
        }
    }

    SteamAuthMode ParseSteamAuthMode(const std::string& mode)
    {
        const std::string m = Lower(mode.c_str());
        if (m == "flexible")
            return SteamAuthMode::Flexible;
        if (m == "strict")
            return SteamAuthMode::Strict;
        if (m != "auto")
            FP_WARN("Unknown SteamAuthMode '{}' in core.json, using \"auto\"", mode);
        return SteamAuthMode::Auto;
    }

    /* =========================
    Resolving
    ========================= */

    // "a.b.*" holds "a.b" and everything below it, never "a.bc"; "*" holds all.
    bool PermissionsManager::Matches(const std::vector<std::string>& held, const std::string& permission)
    {
        for (const std::string& h : held)
        {
            if (h == "*" || h == permission)
                return true;

            if (h.size() >= 2 && h.compare(h.size() - 2, 2, ".*") == 0)
            {
                const size_t baseLen = h.size() - 2;
                if (permission.compare(0, baseLen, h, 0, baseLen) == 0
                    && (permission.size() == baseLen || permission[baseLen] == '.'))
                    return true;
            }
        }
        return false;
    }

    const PermissionsManager::Effective& PermissionsManager::Resolve(uint64 steamId)
    {
        if (auto it = m_cache.find(steamId); it != m_cache.end())
            return it->second;

        Effective eff;
        std::set<std::string> held;
        std::vector<std::string> pending{ kDefaultGroup };

        if (steamId != 0)
        {
            for (const auto& [owner, layer] : m_layers)
            {
                auto pit = layer.players.find(steamId);
                if (pit == layer.players.end())
                    continue;

                held.insert(pit->second.permissions.begin(), pit->second.permissions.end());
                pending.insert(pending.end(), pit->second.groups.begin(), pit->second.groups.end());
                if (pit->second.immunity)
                    eff.immunity = std::max(eff.immunity, *pit->second.immunity);
            }
        }

        // Groups and what they inherit, once each: a cycle just stops.
        while (!pending.empty())
        {
            std::string group = std::move(pending.back());
            pending.pop_back();

            if (!eff.groups.insert(group).second)
                continue;

            for (const auto& [owner, layer] : m_layers)
            {
                auto git = layer.groups.find(group);
                if (git == layer.groups.end())
                    continue;

                held.insert(git->second.permissions.begin(), git->second.permissions.end());
                pending.insert(pending.end(), git->second.parents.begin(), git->second.parents.end());
                if (git->second.immunity)
                    eff.immunity = std::max(eff.immunity, *git->second.immunity);
            }
        }

        eff.root = held.contains("*");
        eff.permissions.assign(held.begin(), held.end());

        return m_cache.emplace(steamId, std::move(eff)).first->second;
    }

    void PermissionsManager::Changed(uint64 steamId)
    {
        m_cache.clear();
        pluginManager.OnPermissionsChanged(steamId);
    }

    // A change that can touch anybody goes out once, on the next frame: a
    // plugin unloading or a file reload is no place to call into plugins.
    void PermissionsManager::ChangedAll()
    {
        m_cache.clear();
        m_bNotifyAll = true;
    }

    /* =========================
    Checks
    ========================= */

    bool PermissionsManager::HasPermission(uint64 steamId, const char* permission)
    {
        if (!permission || !*permission)
            return true;

        return Matches(Resolve(steamId).permissions, Lower(permission));
    }

    bool PermissionsManager::PlayerHasPermission(CPlayerSlot slot, const char* permission)
    {
        if (!slot.IsValid())
            return true;

        return HasPermission(GetPlayerSteamID(slot), permission);
    }

    std::string PermissionsManager::CommandPermission(const std::string& command, const std::string& registered) const
    {
        if (auto it = m_overrides.find(Lower(command.c_str())); it != m_overrides.end())
            return it->second;
        return registered;
    }

    bool PermissionsManager::CheckAccess(CPlayerSlot slot, const char* name, const char* defaultPermission)
    {
        const std::string permission = CommandPermission(name ? name : "", defaultPermission ? defaultPermission : "");
        return PlayerHasPermission(slot, permission.c_str());
    }

    bool PermissionsManager::InGroup(uint64 steamId, const char* group)
    {
        return Resolve(steamId).groups.contains(Lower(group));
    }

    int PermissionsManager::GetImmunity(uint64 steamId)
    {
        return Resolve(steamId).immunity;
    }

    bool PermissionsManager::CanTarget(CPlayerSlot caller, CPlayerSlot target)
    {
        if (!caller.IsValid() || caller == target)
            return true;

        const Effective& callerEff = Resolve(GetPlayerSteamID(caller));
        if (callerEff.root)
            return true;

        const int callerImmunity = callerEff.immunity;
        return callerImmunity >= GetImmunity(GetPlayerSteamID(target));
    }

    PermissionsManager::SlotState* PermissionsManager::Slot(CPlayerSlot slot)
    {
        return IsValidSlot(slot.Get()) ? &m_slots[slot.Get()] : nullptr;
    }

    uint64 PermissionsManager::GetPlayerSteamID(CPlayerSlot slot)
    {
        const SlotState* st = Slot(slot);
        return st && st->connected ? st->steamId : 0;
    }

    bool PermissionsManager::IsPlayerAuthorized(CPlayerSlot slot)
    {
        const SlotState* st = Slot(slot);
        return st && st->connected && st->authorized;
    }

    /* =========================
    Players
    ========================= */

    void PermissionsManager::GrantPermission(PluginId owner, uint64 steamId, const char* permission)
    {
        if (!steamId || !permission || !*permission)
            return;

        if (LayerOf(owner).players[steamId].permissions.insert(Lower(permission)).second)
            Changed(steamId);
    }

    void PermissionsManager::RevokePermission(PluginId owner, uint64 steamId, const char* permission)
    {
        auto lit = m_layers.find(owner);
        if (lit == m_layers.end())
            return;

        auto pit = lit->second.players.find(steamId);
        if (pit != lit->second.players.end() && pit->second.permissions.erase(Lower(permission)))
            Changed(steamId);
    }

    void PermissionsManager::AddToGroup(PluginId owner, uint64 steamId, const char* group)
    {
        if (!steamId || !group || !*group)
            return;

        if (LayerOf(owner).players[steamId].groups.insert(Lower(group)).second)
            Changed(steamId);
    }

    void PermissionsManager::RemoveFromGroup(PluginId owner, uint64 steamId, const char* group)
    {
        auto lit = m_layers.find(owner);
        if (lit == m_layers.end())
            return;

        auto pit = lit->second.players.find(steamId);
        if (pit != lit->second.players.end() && pit->second.groups.erase(Lower(group)))
            Changed(steamId);
    }

    void PermissionsManager::SetImmunity(PluginId owner, uint64 steamId, int immunity)
    {
        if (!steamId)
            return;

        LayerOf(owner).players[steamId].immunity = immunity;
        Changed(steamId);
    }

    void PermissionsManager::ClearPlayer(PluginId owner, uint64 steamId)
    {
        auto lit = m_layers.find(owner);
        if (lit != m_layers.end() && lit->second.players.erase(steamId))
            Changed(steamId);
    }

    /* =========================
    Groups
    ========================= */

    void PermissionsManager::CreateGroup(PluginId owner, const char* group, int immunity)
    {
        if (!group || !*group)
            return;

        LayerOf(owner).groups[Lower(group)].immunity = immunity;
        Changed(0);
    }

    void PermissionsManager::DeleteGroup(PluginId owner, const char* group)
    {
        auto lit = m_layers.find(owner);
        if (lit != m_layers.end() && lit->second.groups.erase(Lower(group)))
            Changed(0);
    }

    void PermissionsManager::AddGroupPermission(PluginId owner, const char* group, const char* permission)
    {
        if (!group || !*group || !permission || !*permission)
            return;

        if (LayerOf(owner).groups[Lower(group)].permissions.insert(Lower(permission)).second)
            Changed(0);
    }

    void PermissionsManager::RemoveGroupPermission(PluginId owner, const char* group, const char* permission)
    {
        auto lit = m_layers.find(owner);
        if (lit == m_layers.end())
            return;

        auto git = lit->second.groups.find(Lower(group));
        if (git != lit->second.groups.end() && git->second.permissions.erase(Lower(permission)))
            Changed(0);
    }

    void PermissionsManager::AddGroupParent(PluginId owner, const char* group, const char* parent)
    {
        if (!group || !*group || !parent || !*parent)
            return;

        if (LayerOf(owner).groups[Lower(group)].parents.insert(Lower(parent)).second)
            Changed(0);
    }

    void PermissionsManager::RemoveGroupParent(PluginId owner, const char* group, const char* parent)
    {
        auto lit = m_layers.find(owner);
        if (lit == m_layers.end())
            return;

        auto git = lit->second.groups.find(Lower(group));
        if (git != lit->second.groups.end() && git->second.parents.erase(Lower(parent)))
            Changed(0);
    }

    /* =========================
    Refused commands
    ========================= */

    Source2ModHookId PermissionsManager::SetAccessDeniedHandler(AccessDeniedHandler handler)
    {
        const PluginId owner = hookid::OwnerOfHandler(handler);
        return m_deniedHandlers.Add(owner, std::move(handler));
    }

    bool PermissionsManager::ClearAccessDeniedHandler(Source2ModHookId id)
    {
        return m_deniedHandlers.RemoveId(id);
    }

    void PermissionsManager::Deny(const Source2ModCommandContext& ctx, const Source2ModCommandArgs& args, const std::string& permission)
    {
        const std::vector<AccessDeniedHandler> handlers = m_deniedHandlers.Snapshot();
        if (!handlers.empty())
        {
            handlers.back()(ctx, args, permission.c_str());
            return;
        }

        commands::commandsManager.ReplyToCommand(ctx, m_deniedMessage.c_str());
    }

    /* =========================
    permissions.json
    ========================= */

    uint64 PermissionsManager::ParseSteamID(const std::string& text)
    {
        if (text.empty())
            return 0;

        // STEAM_X:Y:Z -- the universe X is 0 or 1 depending on who prints it.
        if (text.rfind("STEAM_", 0) == 0)
        {
            unsigned universe = 0, y = 0;
            unsigned long long z = 0;
            if (sscanf(text.c_str(), "STEAM_%u:%u:%llu", &universe, &y, &z) == 3 && y <= 1)
                return kSteamID64Base + z * 2 + y;
            return 0;
        }

        // [U:1:N]
        if (text.rfind("[U:", 0) == 0)
        {
            unsigned universe = 0;
            unsigned long long account = 0;
            if (sscanf(text.c_str(), "[U:%u:%llu]", &universe, &account) == 2)
                return kSteamID64Base + account;
            return 0;
        }

        if (!std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c); }))
            return 0;

        const uint64 value = std::strtoull(text.c_str(), nullptr, 10);
        return value > kSteamID64Base ? value : 0;
    }

    bool PermissionsManager::LoadFile(std::string& error)
    {
        namespace fs = std::filesystem;
        using json = nlohmann::json;

        const std::string path = paths::GetConfigsDirectory() + "/permissions.json";
        const std::string example = paths::GetConfigsDirectory() + "/permissions.example.json";

        std::error_code ec;
        if (!fs::exists(path, ec) && fs::exists(example, ec))
        {
            fs::copy_file(example, path, ec);
            if (!ec)
                FP_INFO("permissions.json not found, created it from the example.");
        }

        Layer layer;
        std::unordered_map<std::string, std::string> overrides;

        std::ifstream ifs(path);
        if (ifs)
        {
            try
            {
                // Comments allowed: it is a file people edit by hand.
                const json root = json::parse(ifs, nullptr, true, true);

                if (auto it = root.find("Groups"); it != root.end() && it->is_object())
                {
                    for (const auto& [name, def] : it->items())
                    {
                        GroupDef& g = layer.groups[Lower(name.c_str())];
                        for (const auto& p : def.value("Permissions", json::array()))
                            g.permissions.insert(Lower(p.get<std::string>().c_str()));
                        for (const auto& p : def.value("Inherits", json::array()))
                            g.parents.insert(Lower(p.get<std::string>().c_str()));
                        if (def.contains("Immunity"))
                            g.immunity = def["Immunity"].get<int>();
                    }
                }

                if (auto it = root.find("Admins"); it != root.end() && it->is_object())
                {
                    for (const auto& [key, def] : it->items())
                    {
                        const uint64 steamId = ParseSteamID(key);
                        if (!steamId)
                        {
                            FP_WARN("permissions.json: '{}' under Admins is not a SteamID, skipped", key);
                            continue;
                        }

                        PlayerDef& p = layer.players[steamId];
                        for (const auto& perm : def.value("Permissions", json::array()))
                            p.permissions.insert(Lower(perm.get<std::string>().c_str()));
                        for (const auto& group : def.value("Groups", json::array()))
                            p.groups.insert(Lower(group.get<std::string>().c_str()));
                        if (def.contains("Immunity"))
                            p.immunity = def["Immunity"].get<int>();
                    }
                }

                if (auto it = root.find("Overrides"); it != root.end() && it->is_object())
                {
                    for (const auto& [command, permission] : it->items())
                        overrides[Lower(command.c_str())] = Lower(permission.get<std::string>().c_str());
                }
            }
            catch (const std::exception& ex)
            {
                error = ex.what();
                return false;
            }
        }

        m_layers[0] = std::move(layer);
        m_overrides = std::move(overrides);
        ChangedAll();
        return true;
    }

    /* =========================
    Lifecycle
    ========================= */

    void PermissionsManager::Init()
    {
        m_authMode = ParseSteamAuthMode(shared::g_pCoreConfig->SteamAuthMode);
        m_deniedMessage = shared::g_pCoreConfig->AccessDeniedMessage.empty() ? kDefaultDeniedMessage : shared::g_pCoreConfig->AccessDeniedMessage;

        std::string error;
        if (!LoadFile(error))
            FP_ERROR("Could not read permissions.json: {}", error);

        // Steam may already be up when the core loads (it is not on a cold
        // start; the activation hook registers it then).
        if (SteamGameServer())
            OnSteamAPIActivated();
    }

    void PermissionsManager::Shutdown()
    {
        OnSteamAPIDeactivated();
        m_layers.clear();
        m_cache.clear();
        m_deniedHandlers.Clear();
    }

    void PermissionsManager::RemoveAllForPlugin(PluginId id)
    {
        m_deniedHandlers.RemoveOwner(id);

        if (id != 0 && m_layers.erase(id))
            ChangedAll();
    }

    /* =========================
    Players coming and going
    ========================= */

    // What "auto" checks: a server that does not validate tickets will not
    // say a player is authenticated any time soon, so waiting would leave
    // everybody without their permissions.
    bool PermissionsManager::UsesClaimedSteamID() const
    {
        switch (m_authMode)
        {
        case SteamAuthMode::Flexible: return true;
        case SteamAuthMode::Strict:   return false;
        case SteamAuthMode::Auto:     break;
        }

        if (CommandLine()->HasParm("-insecure"))
            return true;

        if (ConVarRefAbstract svLan("sv_lan"); svLan.IsValidRef() && svLan.GetBool())
            return true;

        ISteamGameServer* steam = SteamGameServer();
        return !steam || !steam->BLoggedOn();
    }

    void PermissionsManager::Authorize(int slot)
    {
        SlotState& st = m_slots[slot];
        if (st.authorized)
            return;

        const CSteamID* validated = g_pEngineServer->GetClientSteamID(CPlayerSlot(slot));
        const uint64 steamId = validated ? validated->ConvertToUint64() : st.xuid;

        st.authorized = true;
        st.refused = false;

        const bool changed = st.steamId != steamId;
        st.steamId = steamId;

        // Before the plugins: a cookie read in their OnClientAuthorized finds
        // the values loading, and the cached forward follows next frame.
        cookies::cookiesManager.OnClientAuthorized(CPlayerSlot(slot), steamId);
        pluginManager.OnClientAuthorized(CPlayerSlot(slot), steamId);
        if (changed)
            Changed(steamId);
    }

    void PermissionsManager::OnClientPutInServer(CPlayerSlot slot, int type, uint64 xuid)
    {
        SlotState* st = Slot(slot);
        if (!st)
            return;

        // The same player again (a map change puts everybody in once more):
        // nothing to redo, and OnClientAuthorized must not fire twice.
        if (st->connected && st->xuid == xuid && (st->authorized || st->fake))
            return;

        *st = {};
        st->connected = true;
        st->xuid = xuid;
        st->fake = xuid == 0 || type != 0;

        if (st->fake)
            return;

        if (g_pEngineServer->IsClientFullyAuthenticated(slot))
        {
            Authorize(slot.Get());
            return;
        }

        if (UsesClaimedSteamID())
        {
            st->steamId = xuid;
            Changed(xuid);
        }
    }

    void PermissionsManager::OnClientDisconnect(CPlayerSlot slot)
    {
        if (SlotState* st = Slot(slot))
            *st = {};
    }

    void PermissionsManager::OnGameFrame()
    {
        if (m_bNotifyAll)
        {
            m_bNotifyAll = false;
            pluginManager.OnPermissionsChanged(0);
        }

        if (!g_pEngineServer)
            return;

        // Validation has no engine callback a plugin can hook; the ticket
        // response below covers it when Steam answers, this covers the rest.
        for (int i = 0; i < ABSOLUTE_PLAYER_LIMIT; i++)
        {
            const SlotState& st = m_slots[i];
            if (st.connected && !st.fake && !st.authorized && !st.refused
                && g_pEngineServer->IsClientFullyAuthenticated(CPlayerSlot(i)))
                Authorize(i);
        }
    }

    void PermissionsManager::OnSteamAPIActivated()
    {
        if (m_bAuthCallback)
            return;

        m_authCallback.Register(this, &PermissionsManager::OnValidateAuthTicket);
        m_bAuthCallback = true;
    }

    void PermissionsManager::OnSteamAPIDeactivated()
    {
        if (!m_bAuthCallback)
            return;

        m_authCallback.Unregister();
        m_bAuthCallback = false;
    }

    void PermissionsManager::OnValidateAuthTicket(ValidateAuthTicketResponse_t* response)
    {
        const uint64 steamId = response->m_SteamID.ConvertToUint64();

        for (int i = 0; i < ABSOLUTE_PLAYER_LIMIT; i++)
        {
            SlotState& st = m_slots[i];
            if (!st.connected || st.fake || st.xuid != steamId)
                continue;

            if (response->m_eAuthSessionResponse == k_EAuthSessionResponseOK)
            {
                Authorize(i);
                return;
            }

            // A refused ticket: whatever the claimed SteamID was given is taken back.
            st.refused = true;
            st.authorized = false;
            const bool hadPermissions = st.steamId != 0;
            st.steamId = 0;

            FP_WARN("Steam refused the ticket of slot {} ({}), response {}", i, steamId, static_cast<int>(response->m_eAuthSessionResponse));

            pluginManager.OnClientAuthorizeFailed(CPlayerSlot(i), steamId);
            if (hadPermissions)
                Changed(steamId);
            return;
        }
    }

    /* =========================
    s2m admins
    ========================= */

    std::vector<std::string> PermissionsManager::DescribeSteamID(uint64 steamId)
    {
        const Effective& eff = Resolve(steamId);

        std::string groups, perms;
        for (const auto& g : std::set<std::string>(eff.groups.begin(), eff.groups.end()))
            groups += (groups.empty() ? "" : ", ") + g;
        for (const auto& p : eff.permissions)
            perms += (perms.empty() ? "" : ", ") + p;

        return {
            "SteamID: " + std::to_string(steamId),
            "Groups: " + (groups.empty() ? std::string("(none)") : groups),
            "Permissions: " + (perms.empty() ? std::string("(none)") : perms),
            "Immunity: " + std::to_string(eff.immunity) + (eff.root ? " (root)" : ""),
        };
    }

    std::vector<std::string> PermissionsManager::DescribePlayers()
    {
        std::vector<std::string> lines;
        for (int i = 0; i < ABSOLUTE_PLAYER_LIMIT; i++)
        {
            const SlotState& st = m_slots[i];
            if (!st.connected || st.fake)
                continue;

            const char* state = st.authorized ? "authorized" : st.refused ? "REFUSED" : st.steamId ? "claimed" : "waiting for Steam";
            const Effective& eff = Resolve(st.steamId);

            std::string groups;
            for (const auto& g : std::set<std::string>(eff.groups.begin(), eff.groups.end()))
                groups += (groups.empty() ? "" : ",") + g;

            lines.push_back("[" + std::to_string(i) + "] " + std::to_string(st.xuid) + " " + state
                            + " groups=" + groups + " immunity=" + std::to_string(eff.immunity)
                            + (eff.root ? " root" : ""));
        }
        return lines;
    }
}
