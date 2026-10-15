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
#include "core/maps.h"

#include "core/events.h"
#include "core/scheduler.h"
#include "core/shared.h"
#include "utils/log.h"
#include "utils/paths.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace maps
{
    MapsManager mapsManager;

    namespace
    {
        constexpr size_t kHistorySize = 32;

        // Everything that ends up in a server command: a map name has no
        // business holding a separator or a quote.
        bool SafeName(const char* map)
        {
            if (!map || !*map)
                return false;
            for (const char* c = map; *c; c++)
            {
                if (*c == ';' || *c == '"' || *c == '\n' || *c == '\r' || static_cast<unsigned char>(*c) < 0x20)
                    return false;
            }
            return true;
        }

        std::string Trim(const std::string& s)
        {
            size_t a = 0, b = s.size();
            while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
                a++;
            while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
                b--;
            return s.substr(a, b - a);
        }

        /// A map list file: a map per line, the first word of it.
        bool ReadListFile(const std::filesystem::path& file, std::vector<std::string>& maps)
        {
            std::ifstream in(file);
            if (!in)
                return false;

            std::string line;
            while (std::getline(in, line))
            {
                line = Trim(line);
                if (line.empty() || line[0] == '#' || line[0] == ';' || line.rfind("//", 0) == 0)
                    continue;
                const size_t space = line.find_first_of(" \t");
                maps.push_back(space == std::string::npos ? line : line.substr(0, space));
            }
            return true;
        }

        std::filesystem::path GamePath(const std::string& file)
        {
            std::filesystem::path p(file);
            return p.is_absolute() ? p : std::filesystem::path(paths::GameDirectory()) / p;
        }

        bool ReadMapsFolder(std::vector<std::string>& maps)
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            std::vector<std::string> found;
            for (const auto& entry : fs::directory_iterator(fs::path(paths::GameDirectory()) / "maps", ec))
            {
                if (!entry.is_regular_file(ec) || entry.path().extension() != ".vpk")
                    continue;
                std::string name = entry.path().stem().string();
                // A map's extra packages (de_dust2_vanity.vpk) are not maps.
                if (name.size() > 7 && name.compare(name.size() - 7, 7, "_vanity") == 0)
                    continue;
                found.push_back(std::move(name));
            }
            if (found.empty())
                return false;
            std::sort(found.begin(), found.end());
            maps.insert(maps.end(), found.begin(), found.end());
            return true;
        }

        std::string ConVarString(const char* name)
        {
            ConVarRefAbstract ref(name);
            if (!ref.IsValidRef())
                return {};
            return ref.GetString().Get();
        }

        void SetConVarString(const char* name, const char* value)
        {
            ConVarRefAbstract ref(name);
            if (ref.IsValidRef())
                ref.SetString(value);
        }
    }

    bool MapsManager::WorkshopId(const char* map, std::string& id)
    {
        if (!map)
            return false;
        std::string s(map);
        if (s.rfind("workshop/", 0) == 0)
        {
            s = s.substr(9);
            if (const size_t slash = s.find('/'); slash != std::string::npos)
                s = s.substr(0, slash);
        }
        if (s.empty() || !std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return false;
        id = s;
        return true;
    }

    void MapsManager::HookMatchEnd()
    {
        if (m_matchEndHook)
            return;
        m_matchEndHook = events::eventManager.HookGameEvent(0, "cs_win_panel_match", GameEventHandler([this](IGameEvent*, bool, bool&)
        {
            OnMatchEnd();
            return KHook::Action::Ignore;
        }), true);
    }

    void MapsManager::Shutdown()
    {
        if (m_matchEndHook)
            events::eventManager.Unhook(m_matchEndHook);
        m_matchEndHook = 0;
    }

    void MapsManager::OnLevelInit(const char* mapName)
    {
        if (!m_current.empty())
        {
            m_history.push_front({ m_current, m_pendingReason.empty() ? "Normal level change" : m_pendingReason, m_mapStart });
            if (m_history.size() > kHistorySize)
                m_history.pop_back();
        }
        m_current = mapName ? mapName : "";
        m_mapStart = static_cast<int64_t>(std::time(nullptr));
        m_pendingReason.clear();
        // Spent: it was the map we are on now, or the match ended some other way.
        m_next.clear();
    }

    void MapsManager::OnMatchEnd()
    {
        // A map the server has went to `nextlevel` already, and the game
        // changes to it by itself. A Workshop one the game cannot: change
        // to it shortly before the game would restart the match.
        if (m_next.empty() || IsMapValid(m_next.c_str()))
            return;

        float delay = 10.0f;
        if (ConVarRefAbstract restart("mp_match_restart_delay"); restart.IsValidRef())
            delay = restart.GetFloat();
        delay = std::max(1.0f, delay - 1.0f);

        FP_INFO("Match ended: changing to the next map {} in {:.0f}s", m_next, delay);
        scheduler::schedulerManager.AddTimer(0, delay, TimerCallback([this, next = m_next]()
        {
            // Changed or cleared since: leave it.
            if (m_next == next)
                ChangeLevel(next.c_str(), "Next map");
        }), TIMER_FLAG_NO_MAPCHANGE);
    }

    const char* MapsManager::GetCurrentMap()
    {
        // Loaded mid-map: the level was initialised before the core was there.
        if (m_current.empty() && shared::g_pGlobalVars)
        {
            if (const char* name = shared::g_pGlobalVars->mapname.ToCStr(); name && *name)
                m_current = name;
        }
        return m_current.c_str();
    }

    bool MapsManager::IsMapValid(const char* map)
    {
        if (!SafeName(map))
            return false;
        return g_pEngineServer && g_pEngineServer->IsMapValid(map) != 0;
    }

    Source2ModFindMapResult MapsManager::FindMap(const char* map, std::string& found)
    {
        found = map ? map : "";
        if (!SafeName(map))
            return SOURCE2MOD_FINDMAP_NOT_FOUND;

        if (IsMapValid(map))
            return SOURCE2MOD_FINDMAP_FOUND;

        std::string id;
        if (WorkshopId(map, id))
            return SOURCE2MOD_FINDMAP_POSSIBLY_AVAILABLE;

        // A part of a name: the one map of the lists (or the maps folder) it fits.
        std::vector<std::string> candidates;
        ReadMapList("default", candidates, SOURCE2MOD_MAPLIST_MAPSFOLDER);
        ReadMapsFolder(candidates);

        std::string lower(map);
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        std::string match;
        for (const std::string& candidate : candidates)
        {
            std::string c = candidate;
            std::transform(c.begin(), c.end(), c.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (c == lower)
            {
                found = candidate;
                return SOURCE2MOD_FINDMAP_POSSIBLY_AVAILABLE;
            }
            if (c.find(lower) == std::string::npos)
                continue;
            if (!match.empty() && match != candidate)
                return SOURCE2MOD_FINDMAP_NOT_FOUND;   // ambiguous
            match = candidate;
        }

        if (!match.empty())
        {
            found = match;
            return SOURCE2MOD_FINDMAP_FUZZY_MATCH;
        }

        // Not a map of ours: maybe one of the host's Workshop collection.
        return SOURCE2MOD_FINDMAP_NOT_FOUND;
    }

    bool MapsManager::SetNextMap(const char* map)
    {
        if (!SafeName(map))
            return false;

        std::string found;
        const Source2ModFindMapResult result = FindMap(map, found);
        if (result == SOURCE2MOD_FINDMAP_NOT_FOUND)
            return false;

        m_next = found;
        if (IsMapValid(found.c_str()))
            SetConVarString("nextlevel", found.c_str());
        else
            HookMatchEnd();
        return true;
    }

    const char* MapsManager::GetNextMap()
    {
        if (!m_next.empty())
            return m_next.c_str();

        m_nextResult = ConVarString("nextlevel");
        if (!m_nextResult.empty())
            return m_nextResult.c_str();

        // The one after this one in the map cycle.
        std::vector<std::string> cycle;
        if (ReadMapList("mapcyclefile", cycle, 0) && !cycle.empty())
        {
            const std::string current = GetCurrentMap();
            auto it = std::find(cycle.begin(), cycle.end(), current);
            m_nextResult = (it == cycle.end() || it + 1 == cycle.end()) ? cycle.front() : *(it + 1);
        }
        return m_nextResult.c_str();
    }

    bool MapsManager::ChangeLevel(const char* map, const char* reason)
    {
        if (!SafeName(map) || !g_pEngineServer)
            return false;

        std::string id, command;
        if (WorkshopId(map, id))
            command = "host_workshop_map " + id;
        else if (IsMapValid(map))
            command = std::string("changelevel ") + map;
        else
            command = std::string("ds_workshop_changelevel ") + map;

        m_pendingReason = reason && *reason ? reason : "";
        FP_INFO("Changing map to {}{}{}", map, m_pendingReason.empty() ? "" : ": ", m_pendingReason);
        g_pEngineServer->ServerCommand((command + "\n").c_str());
        return true;
    }

    int MapsManager::GetMapHistorySize()
    {
        return static_cast<int>(m_history.size());
    }

    bool MapsManager::GetMapHistory(int index, Source2ModMapHistoryEntry& entry)
    {
        if (index < 0 || static_cast<size_t>(index) >= m_history.size())
            return false;
        entry = m_history[static_cast<size_t>(index)];
        return true;
    }

    bool MapsManager::ReadNamedList(const std::string& name, std::vector<std::string>& maps, int depth)
    {
        if (depth > 8)
        {
            FP_WARN("maplists.json: \"{}\" is part of a target loop", name);
            return false;
        }

        nlohmann::json config;
        {
            std::ifstream in(std::filesystem::path(paths::GetConfigsDirectory()) / "maplists.json");
            if (in)
            {
                try
                {
                    config = nlohmann::json::parse(in, nullptr, true, true);
                }
                catch (const std::exception& e)
                {
                    FP_WARN("maplists.json could not be read: {}", e.what());
                    config = nlohmann::json::object();
                }
            }
        }

        if (config.is_object() && config.contains(name) && config[name].is_object())
        {
            const nlohmann::json& entry = config[name];
            if (entry.contains("target") && entry["target"].is_string())
                return ReadNamedList(entry["target"].get<std::string>(), maps, depth + 1);
            if (entry.contains("maps") && entry["maps"].is_array())
            {
                for (const auto& m : entry["maps"])
                    if (m.is_string() && !m.get<std::string>().empty())
                        maps.push_back(m.get<std::string>());
                return true;
            }
            if (entry.contains("file") && entry["file"].is_string())
                return ReadListFile(GamePath(entry["file"].get<std::string>()), maps);
            FP_WARN("maplists.json: \"{}\" has none of \"target\", \"maps\", \"file\"", name);
            return false;
        }

        if (name == "mapcyclefile")
        {
            std::string file = ConVarString("mapcyclefile");
            if (file.empty())
                file = "mapcycle.txt";
            return ReadListFile(GamePath(file), maps) || ReadListFile(GamePath("cfg/" + file), maps);
        }
        return false;
    }

    bool MapsManager::ReadMapList(const char* name, std::vector<std::string>& maps, int flags)
    {
        const std::string list = name && *name ? name : "default";
        std::vector<std::string> read;

        bool ok = ReadNamedList(list, read, 0);
        if (!ok && !(flags & SOURCE2MOD_MAPLIST_NO_DEFAULT))
        {
            if (list != "default")
                ok = ReadNamedList("default", read, 0);
            if (!ok && list != "mapcyclefile")
                ok = ReadNamedList("mapcyclefile", read, 0);
        }
        if (!ok && (flags & SOURCE2MOD_MAPLIST_MAPSFOLDER))
            ok = ReadMapsFolder(read);

        if (!ok)
            return false;
        maps.insert(maps.end(), read.begin(), read.end());
        return true;
    }
}
