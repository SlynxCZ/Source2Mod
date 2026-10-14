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

#include <source2mod/ISource2ModPaths.h>

#include <string>
#include <unordered_map>

namespace paths
{
    class PathsManager : public ISource2ModPaths
    {
    public:
        const char* GameDirectory() override;
        const char* RootDirectory() override;
        const char* ConfigsDirectory() override;
        const char* GamedataDirectory() override;
        const char* PluginsDirectory() override;
        const char* SharedDirectory() override;
        const char* PluginConfigDirectory(PluginId owner) override;

    private:
        // The interface hands out const char*, so the strings have to outlive
        // the call. These are built once and kept.
        std::string m_sGame, m_sRoot, m_sConfigs, m_sGamedata, m_sPlugins, m_sShared;
        std::unordered_map<PluginId, std::string> m_PluginConfigs;
    };

    /// Creates any of Source2Mod's directories that are not there yet.
    ///
    /// A fresh install has none of them, and an empty plugins folder is not an
    /// error -- it used to fail the whole load.
    void EnsureLayout();

    extern PathsManager pathsManager;
}
