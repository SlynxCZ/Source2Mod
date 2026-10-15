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
 * Authors:
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */
#pragma once

#include <source2mod/ISource2ModMaps.h>

#include <deque>
#include <string>
#include <vector>

namespace maps
{
    /// ISource2ModMaps: the current and next map, map changes, map history
    /// and the map lists of configs/maplists.json.
    class MapsManager : public ISource2ModMaps
    {
    public:
        const char* GetCurrentMap() override;
        bool IsMapValid(const char* map) override;
        Source2ModFindMapResult FindMap(const char* map, std::string& found) override;
        bool SetNextMap(const char* map) override;
        const char* GetNextMap() override;
        bool ChangeLevel(const char* map, const char* reason) override;
        int GetMapHistorySize() override;
        bool GetMapHistory(int index, Source2ModMapHistoryEntry& entry) override;
        bool ReadMapList(const char* name, std::vector<std::string>& maps, int flags) override;

        /// Takes the match end hook back out.
        void Shutdown();

        /// The core's OnLevelInit, before the plugins hear of it.
        void OnLevelInit(const char* mapName);

        /// "workshop/123", "workshop/123/name" or "123": true and the ID.
        static bool WorkshopId(const char* map, std::string& id);

    private:
        bool ReadNamedList(const std::string& name, std::vector<std::string>& maps, int depth);
        void HookMatchEnd();
        void OnMatchEnd();

        std::string m_current;
        std::string m_next;
        std::string m_nextResult;
        std::string m_pendingReason;
        int64_t m_mapStart = 0;
        std::deque<Source2ModMapHistoryEntry> m_history;
        int m_matchEndHook = 0;
    };

    extern MapsManager mapsManager;
}
