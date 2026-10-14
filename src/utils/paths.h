/**
* vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * Copyright (C) 2025-2026 Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl.
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
 * As a special exception, Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
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
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */
#pragma once

#include "core/shared.h"

#include <engine2/isource2server.h>
#include <engine2/ivengineserver2.h>

#include <string>

namespace paths {
    static std::string gameDirectory;

    inline std::string GameDirectory() {
        if (gameDirectory.empty()) {
            CBufferStringGrowable<255> gamePath;
            g_pEngineServer->GetGameDir(gamePath);
            gameDirectory = std::string(gamePath.Get());
        }
        return gameDirectory;
    }

    inline std::string GetRootDirectory() { return GameDirectory() + "/addons/source2mod"; }
    inline std::string GetConfigsDirectory() { return GameDirectory() + "/addons/source2mod/configs"; }
    inline std::string GetGamedataDirectory() { return GameDirectory() + "/addons/source2mod/gamedata"; }
    inline std::string GetPluginsDirectory() { return GameDirectory() + "/addons/source2mod/plugins"; }
    inline std::string GetSharedDirectory() { return GameDirectory() + "/addons/source2mod/shared"; }
    /// Where the crash handler writes its minidumps and crash .txt files.
    inline std::string GetDumpsDirectory() { return GameDirectory() + "/addons/source2mod/dumps"; }

    /// Where a plugin's own configs live: configs/<PluginName>.
    inline std::string GetPluginConfigDirectory(const std::string& name) { return GetConfigsDirectory() + "/" + name; }
    inline std::string EnginePath() { return GameDirectory() + "../bin/linuxsteamrt64/libengine2.so"; }
    inline std::string Tier0Path() { return GameDirectory() + "../bin/linuxsteamrt64/libtier0.so"; }
    inline std::string ServerPath() { return GameDirectory() + "/bin/linuxsteamrt64/libserver.so"; }
    inline std::string SchemaSystemPath() { return GameDirectory() + "../bin/linuxsteamrt64/libschemasystem.so"; }
    inline std::string VScriptPath() { return GameDirectory() + "../bin/linuxsteamrt64/libvscript.so"; }
} // namespace paths