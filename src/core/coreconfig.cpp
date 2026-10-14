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

#include "core/coreconfig.h"

#include "utils/log.h"

#include <tier0_strtools.h>

#include <fstream>

CCoreConfig::CCoreConfig(const std::string& path) { m_sPath = path; }

CCoreConfig::~CCoreConfig() = default;

bool CCoreConfig::Init(char* conf_error, int conf_error_size)
{
    std::ifstream ifs(std::string(m_sPath + ".json"));

    if (!ifs)
    {
        std::ifstream exampleIfs(std::string(m_sPath + ".example.json"));

        if (!exampleIfs)
        {
            V_snprintf(conf_error, conf_error_size, "CoreConfig file not found.");
            return false;
        }

        FP_INFO("CoreConfig file not found, creating one from example.");
        std::ofstream ofs(std::string(m_sPath + ".json"));
        ofs << exampleIfs.rdbuf();
        ofs.close();

        return Init(conf_error, conf_error_size);
    }

    try
    {
        // Comments allowed: it is a file people edit by hand.
        m_json = json::parse(ifs, nullptr, true, true);


        PublicChatTrigger = m_json.value("PublicChatTrigger", PublicChatTrigger);
        SilentChatTrigger = m_json.value("SilentChatTrigger", SilentChatTrigger);
        PluginHotReloadEnabled = m_json.value("PluginHotReloadEnabled", PluginHotReloadEnabled);
        PluginAutoLoadEnabled = m_json.value("PluginAutoLoadEnabled", PluginAutoLoadEnabled);
        UnlockConCommands = m_json.value("UnlockConCommands", UnlockConCommands);
        UnlockConVars = m_json.value("UnlockConVars", UnlockConVars);
        CrashHandlerEnabled = m_json.value("CrashHandlerEnabled", CrashHandlerEnabled);
        CrashDiscordWebhook = m_json.value("CrashDiscordWebhook", CrashDiscordWebhook);
        SlowCallbackWarnMs = m_json.value("SlowCallbackWarnMs", SlowCallbackWarnMs);
        SteamAuthMode = m_json.value("SteamAuthMode", SteamAuthMode);
        AccessDeniedMessage = m_json.value("AccessDeniedMessage", AccessDeniedMessage);
    }
    catch (const std::exception& ex)
    {
        V_snprintf(conf_error, conf_error_size, "Failed to parse CoreConfig file: %s", ex.what());
        return false;
    }

    return true;
}

const std::string CCoreConfig::GetPath() const { return m_sPath; }

bool CCoreConfig::IsTriggerInternal(std::vector<std::string> triggers, const std::string& message,
                                    std::string& prefix) const
{
    for (std::string& trigger : triggers)
    {
        if (message.rfind(trigger, 0) == 0)
        {
            prefix = trigger;
            return true;
        }
    }

    return false;
}

bool CCoreConfig::IsSilentChatTrigger(const std::string& message, std::string& prefix) const
{
    return IsTriggerInternal(SilentChatTrigger, message, prefix);
}

bool CCoreConfig::IsPublicChatTrigger(const std::string& message, std::string& prefix) const
{
    return IsTriggerInternal(PublicChatTrigger, message, prefix);
}
