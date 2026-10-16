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
#include "core/paths.h"
#include "utils/paths.h"

#include "core/pluginmanager.h"
#include "utils/log.h"

#include <filesystem>

namespace paths
{
    PathsManager pathsManager;

    void EnsureLayout()
    {
        namespace fs = std::filesystem;

        for (const auto& dir : { GetRootDirectory(), GetConfigsDirectory(), GetGamedataDirectory(),
                                 GetPluginsDirectory(), GetSharedDirectory(), GetDumpsDirectory() })
        {
            std::error_code ec;

            if (fs::exists(dir, ec))
                continue;

            if (fs::create_directories(dir, ec); ec)
                FP_WARN("Could not create '{}': {}", dir, ec.message());
            else
                FP_INFO("Created '{}'", dir);
        }
    }

    namespace
    {
        // Built on first use: the game directory is not known until the engine
        // is up, so none of these can be initialised statically.
        const char* Cached(std::string& slot, std::string (*build)())
        {
            if (slot.empty())
                slot = build();

            return slot.c_str();
        }
    }

    const char* PathsManager::GameDirectory()     { return Cached(m_sGame,     &paths::GameDirectory); }
    const char* PathsManager::RootDirectory()     { return Cached(m_sRoot,     &paths::GetRootDirectory); }
    const char* PathsManager::ConfigsDirectory()  { return Cached(m_sConfigs,  &paths::GetConfigsDirectory); }
    const char* PathsManager::GamedataDirectory() { return Cached(m_sGamedata, &paths::GetGamedataDirectory); }
    const char* PathsManager::PluginsDirectory()  { return Cached(m_sPlugins,  &paths::GetPluginsDirectory); }
    const char* PathsManager::SharedDirectory()   { return Cached(m_sShared,   &paths::GetSharedDirectory); }

    const char* PathsManager::PluginConfigDirectory(PluginId owner)
    {
        if (auto it = m_PluginConfigs.find(owner); it != m_PluginConfigs.end())
            return it->second.c_str();

        for (auto& plugin : pluginManager.m_plugins)
        {
            if (!plugin || plugin->id != owner || !plugin->api)
                continue;

            const char* pszName = plugin->api->GetName();
            if (!pszName || !*pszName)
                return nullptr;

            auto [iter, _] = m_PluginConfigs.emplace(owner, GetPluginConfigDirectory(pszName));
            return iter->second.c_str();
        }

        return nullptr;
    }
}
