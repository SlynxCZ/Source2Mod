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
 * Command text to players (ISource2ModTargeting). See targeting.h.
 */
#include "targeting.h"

#include "clients.h"
#include "hookid.h"
#include "permissions.h"
#include "raytrace.h"
#include "shared.h"

#include <interfaces/interfaces.h>
#include <source2mod/schema/entity/classes/CCSPlayerController.h>
#include <source2mod/schema/entity/classes/CCSPlayerPawn.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace targeting
{
    TargetingManager targetingManager;

    namespace
    {
        constexpr int kMaxSlots = 64;
        constexpr int kTeamSpectator = 1;
        constexpr int kTeamT = 2;
        constexpr int kTeamCT = 3;

        bool EqualNoCase(const std::string& a, const std::string& b)
        {
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
                [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
        }

        bool ContainsNoCase(const std::string& text, const std::string& part)
        {
            auto it = std::search(text.begin(), text.end(), part.begin(), part.end(),
                [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
            return it != text.end();
        }

        CCSPlayerController* PlayerAt(int slot)
        {
            return clients::ClientsManager::Connected(CPlayerSlot(slot));
        }

        std::string NameOf(CCSPlayerController* player)
        {
            const char* name = player ? player->m_iszPlayerName() : nullptr;
            return name ? name : "";
        }

        int UserIdOf(int slot)
        {
            return g_pEngineServer ? g_pEngineServer->GetPlayerUserId(CPlayerSlot(slot)).Get() : -1;
        }

        bool IsAlive(CCSPlayerController* player)
        {
            return player && player->m_bPawnIsAlive();
        }

        /// The player under the caller's crosshair, or -1.
        int AimTarget(CPlayerSlot caller)
        {
            CCSPlayerController* player = PlayerAt(caller.Get());
            CCSPlayerPawn* pawn = player ? player->GetPlayerPawn() : nullptr;
            if (!pawn)
                return -1;
            TraceOptions options;
            options.InteractsWith = MASK_SHOT;
            const TraceResult tr = raytrace::rayTrace.TraceShape(pawn->GetEyePosition(), pawn->GetEyeAngles(), pawn, &options);
            auto* hit = static_cast<CBaseEntity*>(tr.HitEntity());
            if (!hit)
                return -1;
            for (int slot = 0; slot < kMaxSlots; slot++)
            {
                CCSPlayerController* other = PlayerAt(slot);
                if (other && slot != caller.Get() && other->GetPlayerPawn() == hit)
                    return slot;
            }
            return -1;
        }

        struct Pattern
        {
            const char* name;    // without the @
            const char* phrase;  // in common.phrases
        };

        constexpr Pattern kPatterns[] = {
            { "all", "all players" },
            { "t", "all terrorists" },
            { "ct", "all counter-terrorists" },
            { "spec", "all spectators" },
            { "alive", "all alive players" },
            { "dead", "all dead players" },
            { "bots", "all bots" },
            { "humans", "all humans" },
            { "!me", "all but yourself" },
        };

        /// Whether a slot belongs to a built-in multi-player pattern.
        bool InPattern(const std::string& name, int slot, CCSPlayerController* player, CPlayerSlot caller)
        {
            if (name == "all")
                return true;
            if (name == "t")
                return player->m_iTeamNum() == kTeamT;
            if (name == "ct")
                return player->m_iTeamNum() == kTeamCT;
            if (name == "spec")
                return player->m_iTeamNum() == kTeamSpectator;
            if (name == "alive")
                return IsAlive(player);
            if (name == "dead")
                return !IsAlive(player);
            if (name == "bots")
                return player->IsBot();
            if (name == "humans")
                return !player->IsBot();
            if (name == "!me")
                return slot != caller.Get();
            return false;
        }

        /// Applies the filters to one candidate: 1 = keep, <= 0 an error for a lone target.
        int Check(int slot, CPlayerSlot caller, int flags)
        {
            CCSPlayerController* player = PlayerAt(slot);
            if (!player)
                return SOURCE2MOD_TARGET_NONE;
            if (!(flags & SOURCE2MOD_TARGET_CONNECTED) && player->m_iConnected() != PlayerConnectedState::Connected)
                return SOURCE2MOD_TARGET_NOT_IN_GAME;
            if ((flags & SOURCE2MOD_TARGET_NO_BOTS) && player->IsBot())
                return SOURCE2MOD_TARGET_NOT_HUMAN;
            if ((flags & SOURCE2MOD_TARGET_ALIVE) && !IsAlive(player))
                return SOURCE2MOD_TARGET_NOT_ALIVE;
            if ((flags & SOURCE2MOD_TARGET_DEAD) && IsAlive(player))
                return SOURCE2MOD_TARGET_NOT_DEAD;
            if (!(flags & SOURCE2MOD_TARGET_NO_IMMUNITY) && caller.Get() >= 0 && slot != caller.Get()
                && !permissions::permissionsManager.CanTarget(caller, CPlayerSlot(slot)))
                return SOURCE2MOD_TARGET_IMMUNE;
            return 1;
        }

        /// One named player through the filters.
        int Single(int slot, CPlayerSlot caller, int flags, std::vector<CPlayerSlot>& targets, Source2ModTargetName* name)
        {
            const int result = Check(slot, caller, flags);
            if (result <= 0)
                return result;
            targets.push_back(CPlayerSlot(slot));
            if (name)
            {
                name->text = NameOf(PlayerAt(slot));
                name->isPhrase = false;
            }
            return 1;
        }
    }

    int TargetingManager::ProcessTargetString(const char* pattern, CPlayerSlot caller, int flags,
                                              std::vector<CPlayerSlot>& targets, Source2ModTargetName* name)
    {
        targets.clear();
        const std::string text = pattern ? pattern : "";
        if (text.empty())
            return SOURCE2MOD_TARGET_NONE;

        // @ patterns
        if (text[0] == '@')
        {
            const std::string what = text.substr(1);
            if (what == "me")
                return caller.Get() >= 0 ? Single(caller.Get(), caller, flags | SOURCE2MOD_TARGET_NO_IMMUNITY, targets, name) : SOURCE2MOD_TARGET_NONE;
            if (what == "aim")
            {
                const int slot = caller.Get() >= 0 ? AimTarget(caller) : -1;
                return slot >= 0 ? Single(slot, caller, flags, targets, name) : SOURCE2MOD_TARGET_NONE;
            }

            const Pattern* builtin = nullptr;
            for (const Pattern& p : kPatterns)
            {
                if (EqualNoCase(what, p.name))
                    builtin = &p;
            }
            const Filter* custom = nullptr;
            for (const Filter& f : m_filters)
            {
                if (EqualNoCase(what, f.name))
                    custom = &f;
            }
            if (!builtin && !custom)
                return SOURCE2MOD_TARGET_NONE;
            if (flags & SOURCE2MOD_TARGET_NO_MULTI)
                return SOURCE2MOD_TARGET_AMBIGUOUS;

            std::vector<CPlayerSlot> candidates;
            if (builtin)
            {
                const std::string lower = builtin->name;
                for (int slot = 0; slot < kMaxSlots; slot++)
                {
                    CCSPlayerController* player = PlayerAt(slot);
                    if (player && InPattern(lower, slot, player, caller))
                        candidates.push_back(CPlayerSlot(slot));
                }
            }
            else
            {
                // The filter runs plugin code; a copy, since it may remove filters.
                const Source2ModTargetFilter filter = custom->filter;
                const std::string phrase = custom->phrase;
                const bool isPhrase = custom->phraseIsTranslation;
                if (!filter(caller, candidates))
                    candidates.clear();
                if (name)
                {
                    name->text = phrase;
                    name->isPhrase = isPhrase;
                }
                std::sort(candidates.begin(), candidates.end(), [](CPlayerSlot a, CPlayerSlot b) { return a.Get() < b.Get(); });
                candidates.erase(std::unique(candidates.begin(), candidates.end(), [](CPlayerSlot a, CPlayerSlot b) { return a.Get() == b.Get(); }), candidates.end());
            }

            for (CPlayerSlot slot : candidates)
            {
                if (Check(slot.Get(), caller, flags) > 0)
                    targets.push_back(slot);
            }
            if (builtin && name)
            {
                name->text = builtin->phrase;
                name->isPhrase = true;
            }
            return targets.empty() ? SOURCE2MOD_TARGET_EMPTY_FILTER : static_cast<int>(targets.size());
        }

        // #userid, #SteamID, #exact name
        if (text[0] == '#' && text.size() > 1)
        {
            const std::string rest = text.substr(1);
            char* end = nullptr;
            const long userid = std::strtol(rest.c_str(), &end, 10);
            if (end && !*end)
            {
                for (int slot = 0; slot < kMaxSlots; slot++)
                {
                    if (PlayerAt(slot) && UserIdOf(slot) == userid)
                        return Single(slot, caller, flags, targets, name);
                }
            }
            if (const uint64_t steamId = clients::ClientsManager::ParseSteamId(rest.c_str()))
            {
                for (int slot = 0; slot < kMaxSlots; slot++)
                {
                    CCSPlayerController* player = PlayerAt(slot);
                    if (player && !player->IsBot() && player->m_steamID() == steamId)
                        return Single(slot, caller, flags, targets, name);
                }
                return SOURCE2MOD_TARGET_NONE;
            }
            for (int slot = 0; slot < kMaxSlots; slot++)
            {
                if (PlayerAt(slot) && NameOf(PlayerAt(slot)) == rest)
                    return Single(slot, caller, flags, targets, name);
            }
            return SOURCE2MOD_TARGET_NONE;
        }

        // A bare SteamID
        if (const uint64_t steamId = clients::ClientsManager::ParseSteamId(text.c_str()))
        {
            for (int slot = 0; slot < kMaxSlots; slot++)
            {
                CCSPlayerController* player = PlayerAt(slot);
                if (player && !player->IsBot() && player->m_steamID() == steamId)
                    return Single(slot, caller, flags, targets, name);
            }
        }

        // Part of a name: an exact (case-insensitive) match wins.
        std::vector<int> partial;
        for (int slot = 0; slot < kMaxSlots; slot++)
        {
            CCSPlayerController* player = PlayerAt(slot);
            if (!player)
                continue;
            const std::string playerName = NameOf(player);
            if (EqualNoCase(playerName, text))
                return Single(slot, caller, flags, targets, name);
            if (ContainsNoCase(playerName, text))
                partial.push_back(slot);
        }
        if (partial.empty())
            return SOURCE2MOD_TARGET_NONE;
        if (partial.size() > 1)
            return SOURCE2MOD_TARGET_AMBIGUOUS;
        return Single(partial[0], caller, flags, targets, name);
    }

    int TargetingManager::FindTarget(const char* pattern, CPlayerSlot caller, int flags, CPlayerSlot& target)
    {
        std::vector<CPlayerSlot> targets;
        const int result = ProcessTargetString(pattern, caller, flags | SOURCE2MOD_TARGET_NO_MULTI, targets);
        if (result == 1)
            target = targets[0];
        return result;
    }

    const char* TargetingManager::GetTargetErrorPhrase(int error)
    {
        switch (error)
        {
        case SOURCE2MOD_TARGET_NOT_ALIVE: return "Target must be alive";
        case SOURCE2MOD_TARGET_NOT_DEAD: return "Target must be dead";
        case SOURCE2MOD_TARGET_NOT_IN_GAME: return "Target is not in game";
        case SOURCE2MOD_TARGET_IMMUNE: return "Unable to target";
        case SOURCE2MOD_TARGET_EMPTY_FILTER: return "No matching clients";
        case SOURCE2MOD_TARGET_NOT_HUMAN: return "Cannot target bot";
        case SOURCE2MOD_TARGET_AMBIGUOUS: return "More than one client matched";
        default: return "No matching client";
        }
    }

    const char* TargetingManager::GetTargetErrorText(int error)
    {
        switch (error)
        {
        case SOURCE2MOD_TARGET_NOT_ALIVE: return "This command can only be used on alive players.";
        case SOURCE2MOD_TARGET_NOT_DEAD: return "This command can only be used on dead players.";
        case SOURCE2MOD_TARGET_NOT_IN_GAME: return "The given player is not fully in-game yet.";
        case SOURCE2MOD_TARGET_IMMUNE: return "You cannot target this player.";
        case SOURCE2MOD_TARGET_EMPTY_FILTER: return "No matching clients were found.";
        case SOURCE2MOD_TARGET_NOT_HUMAN: return "This command cannot target bots.";
        case SOURCE2MOD_TARGET_AMBIGUOUS: return "More than one client matched the given pattern.";
        default: return "No matching client was found.";
        }
    }

    Source2ModTargetFilterId TargetingManager::AddTargetFilter(const char* name, Source2ModTargetFilter filter, const char* phrase, bool phraseIsTranslation)
    {
        const PluginId owner = hookid::OwnerOfHandler(filter);
        return AddTargetFilterFor(owner, name, std::move(filter), phrase, phraseIsTranslation);
    }

    Source2ModHookId TargetingManager::AddTargetFilterFor(PluginId owner, const char* name, Source2ModTargetFilter filter, const char* phrase, bool phraseIsTranslation)
    {
        if (!name || !filter)
            return 0;
        std::string bare = name[0] == '@' ? name + 1 : name;
        if (bare.empty() || bare == "me" || bare == "aim")
            return 0;
        for (const Pattern& p : kPatterns)
        {
            if (EqualNoCase(bare, p.name))
                return 0;
        }
        for (const Filter& f : m_filters)
        {
            if (EqualNoCase(bare, f.name))
                return 0;
        }
        const Source2ModHookId id = hookid::Next();
        m_filters.push_back({ owner, id, std::move(bare), phrase ? phrase : "", phraseIsTranslation, std::move(filter) });
        return id;
    }

    bool TargetingManager::RemoveTargetFilter(Source2ModTargetFilterId id)
    {
        const auto before = m_filters.size();
        std::erase_if(m_filters, [id](const Filter& f) { return f.id == id; });
        return m_filters.size() != before;
    }

    void TargetingManager::RemoveAllForPlugin(PluginId id)
    {
        std::erase_if(m_filters, [id](const Filter& f) { return f.owner == id; });
    }
}
