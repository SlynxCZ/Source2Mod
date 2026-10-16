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
 * Command text to players (ISource2ModTargeting): @ patterns, #userid,
 * SteamIDs, names, filters plugins add, immunity through permissions.
 */
#pragma once

#include <source2mod/ISource2ModTargeting.h>

#include <string>
#include <vector>

namespace targeting
{
    class TargetingManager final : public ISource2ModTargeting
    {
    public:
        int ProcessTargetString(const char* pattern, CPlayerSlot caller, int flags,
                                std::vector<CPlayerSlot>& targets, Source2ModTargetName* name = nullptr) override;
        int FindTarget(const char* pattern, CPlayerSlot caller, int flags, CPlayerSlot& target) override;
        const char* GetTargetErrorPhrase(int error) override;
        const char* GetTargetErrorText(int error) override;
        Source2ModHookId AddTargetFilter(const char* name, Source2ModTargetFilter filter, const char* phrase, bool phraseIsTranslation) override;
        bool RemoveTargetFilter(Source2ModHookId id) override;

        // ---- core side ----

        /// AddTargetFilter for an owner the callback's code does not tell (scripts).
        Source2ModHookId AddTargetFilterFor(PluginId owner, const char* name, Source2ModTargetFilter filter, const char* phrase, bool phraseIsTranslation);
        void RemoveAllForPlugin(PluginId id);

    private:
        struct Filter
        {
            PluginId owner;
            Source2ModHookId id;
            std::string name;     // without the @
            std::string phrase;
            bool phraseIsTranslation;
            Source2ModTargetFilter filter;
        };

        std::vector<Filter> m_filters;
    };

    extern TargetingManager targetingManager;
}
