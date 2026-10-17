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

#include <nlohmann/json.hpp>

#include <string>

class CCoreConfig
{
public:
    std::vector<std::string> PublicChatTrigger = { std::string("!") };
    std::vector<std::string> SilentChatTrigger = { std::string("/") };
    bool PluginHotReloadEnabled = true;
    bool PluginAutoLoadEnabled = true;
    bool UnlockConCommands = true;
    bool UnlockConVars = true;

    /// Breakpad crash handler (Linux only): minidumps, local symbolization
    /// and, when the webhook is set, a report posted to Discord.
    bool CrashHandlerEnabled = true;
    std::string CrashDiscordWebhook;

    /// Panorama layouts (panorama/layout/custom_game/<name>.vxml_c on the
    /// client) the core draws HUD menus and HUD texts with; the reference
    /// ones are in the repository's panorama/ folder.

    /// A plugin callback (command, event, timer, menu option, game hook,
    /// listener, HTTP callback) that takes longer than this many
    /// milliseconds is logged with its name and plugin. 0 turns it off.
    double SlowCallbackWarnMs = 100.0;

    /// Which SteamID a connected player's permissions are looked up under:
    /// "auto" (the claimed one when the server does not validate Steam
    /// tickets, else only once Steam validated it), "flexible" (always the
    /// claimed one right away) or "strict" (always the validated one).
    std::string SteamAuthMode = "auto";

    /// What a player refused a command is told, unless a plugin set its own
    /// handler (ISource2ModPermissions::SetAccessDeniedHandler).
    std::string AccessDeniedMessage = "You do not have access to this command.";
    /// The server's language code (configs/languages.cfg), SourceMod's ServerLang.
    std::string ServerLang = "en";

    /// The GeoLite2 database (.mmdb) GeoIP lookups read; relative paths are
    /// under addons/source2mod. GeoLite2-City and GeoLite2-Country ship in shared/.
    std::string GeoIPDatabase = "shared/GeoLite2-City.mmdb";

    /// The SQLite file client preferences (cookies) are kept in; relative
    /// paths are under addons/source2mod.
    std::string ClientPrefsDatabase = "data/clientprefs.sq3";

    using json = nlohmann::json;
    CCoreConfig(const std::string& path);
    ~CCoreConfig();

    bool Init(char* conf_error, int conf_error_size);
    const std::string GetPath() const;

    bool IsSilentChatTrigger(const std::string& message, std::string& prefix) const;
    bool IsPublicChatTrigger(const std::string& message, std::string& prefix) const;

private:
    bool IsTriggerInternal(std::vector<std::string> triggers, const std::string& message, std::string& prefix) const;

private:
    std::string m_sPath;
    json m_json;
};