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
#include "gameconfig.h"

#include "module.h"
#include "plugin.h"
#include "utils/log.h"

#include <cstring>
#include <fstream>

CGameConfig::CGameConfig(const std::string& path) { m_sPath = path; }

CGameConfig::~CGameConfig() = default;

bool CGameConfig::LoadSingle(const std::string& path, char* conf_error, int conf_error_size)
{
    std::ifstream ifs(path);
    if (!ifs)
    {
        V_snprintf(conf_error, conf_error_size, "Failed to open: %s", path.c_str());
        return false;
    }

#if _WIN32
    constexpr auto platform = "windows";
#else
    constexpr auto platform = "linux";
#endif

    try
    {
        // Inside the try: a malformed file is a load error, not an exception
        // escaping into startup.
        json j = json::parse(ifs);

        for (auto& [k, v] : j.items())
        {
            if (v.contains("signatures"))
            {
                if (auto library = v["signatures"]["library"]; library.is_string())
                {
                    m_umLibraries.try_emplace(k, library.get<std::string>());
                }

                if (auto signature = v["signatures"][platform]; signature.is_string())
                {
                    m_umSignatures.try_emplace(k, signature.get<std::string>());
                }
            }

            if (v.contains("offsets"))
            {
                if (auto offset = v["offsets"][platform]; offset.is_number_integer())
                {
                    m_umOffsets.try_emplace(k, offset.get<std::int64_t>());
                }
            }

            if (v.contains("patches"))
            {
                if (auto patch = v["patches"][platform]; patch.is_string())
                {
                    m_umPatches.try_emplace(k, patch.get<std::string>());
                }
            }
        }
    }
    catch (const std::exception& ex)
    {
        V_snprintf(conf_error, conf_error_size, "Parse error in %s: %s", path.c_str(), ex.what());
        return false;
    }

    return true;
}

bool CGameConfig::InitAll(const std::string& folder, char* conf_error, int conf_error_size)
{
    namespace fs = std::filesystem;

    std::vector<fs::path> files;

    // directory_iterator throws on a folder that is not there, and a source2mod
    // with no gamedata is a source2mod that resolves nothing rather than one that
    // fails to start.
    std::error_code ec;
    if (!fs::exists(folder, ec) || !fs::is_directory(folder, ec))
    {
        FP_WARN("No gamedata folder at '{}'; nothing to resolve.", folder);
        return true;
    }

    for (const auto& entry : fs::directory_iterator(folder, ec))
    {
        if (entry.path().extension() == ".json")
        {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b)
    {
        if (a.filename() == "gamedata.json") return true;
        if (b.filename() == "gamedata.json") return false;
        return a.filename() < b.filename();
    });

    for (const auto& file : files)
    {
        if (!LoadSingle(file.string(), conf_error, conf_error_size))
        {
            return false;
        }
    }

    return true;
}

const std::string CGameConfig::GetPath() { return m_sPath; }

const char* CGameConfig::GetLibrary(const char* pchName)
{
    auto it = m_umLibraries.find(pchName);
    if (it == m_umLibraries.end())
    {
        return nullptr;
    }
    return it->second.c_str();
}

const char* CGameConfig::GetSignature(const char* pchName)
{
    auto it = m_umSignatures.find(pchName);
    if (it == m_umSignatures.end())
    {
        return nullptr;
    }
    return it->second.c_str();
}

const char* CGameConfig::GetSymbol(const char* pchName)
{
    // A symbol entry is stored in the signature field, marked with a leading
    // "@"; the symbol itself is everything after it.
    const char* pszSymbol = GetSignature(pchName);

    if (!pszSymbol || std::strlen(pszSymbol) <= 1)
    {
        FP_ERROR("Missing symbol: {}", pchName ? pchName : "(null)");
        return nullptr;
    }

    return pszSymbol + 1;
}

bool CGameConfig::IsSymbol(const char* pchName)
{
    const char* pszSigOrSymbol = GetSignature(pchName);

    if (!pszSigOrSymbol || pszSigOrSymbol[0] == '\0')
    {
        FP_ERROR("Missing signature or symbol: {}", pchName ? pchName : "(null)");
        return false;
    }

    return pszSigOrSymbol[0] == '@';
}

DynLibUtils::CModule* CGameConfig::GetModule(const char* pchName)
{
    const char* pszLibrary = GetLibrary(pchName);
    if (!pszLibrary)
        return nullptr;

    // engine2 is the module's real name; "engine" is the spelling gamedata
    // files have always used for it.
    std::string sModule = pszLibrary;
    if (sModule == "engine")
        sModule = "engine2";

    if (auto it = m_umModules.find(sModule); it != m_umModules.end())
        return &it->second;

    auto [it, inserted] = m_umModules.try_emplace(sModule);
    if (!InitModuleFromName(it->second, sModule))
    {
        m_umModules.erase(it);
        FP_ERROR("Failed to load module '{}' for {}", sModule, pchName ? pchName : "(null)");
        return nullptr;
    }

    return &it->second;
}

ISource2ModMemory CGameConfig::ResolveSignature(const char* pchName)
{
    DynLibUtils::CModule* pModule = GetModule(pchName);
    if (!pModule)
    {
        FP_ERROR("Invalid module for {}", pchName ? pchName : "(null)");
        return ISource2ModMemory();
    }

    void* pAddress = nullptr;

    if (IsSymbol(pchName))
    {
        const char* pszSymbol = GetSymbol(pchName);
        if (!pszSymbol)
        {
            FP_ERROR("Invalid symbol for {}", pchName ? pchName : "(null)");
            return ISource2ModMemory();
        }

        pAddress = pModule->GetFunctionByName(pszSymbol).GetPtr();
    }
    else
    {
        const char* pszSignature = GetSignature(pchName);
        if (!pszSignature)
        {
            FP_ERROR("Failed to find signature for {}", pchName ? pchName : "(null)");
            return ISource2ModMemory();
        }

        pAddress = pModule->FindPattern(DynLibUtils::ParsePattern(pszSignature)).GetPtr();
    }

    if (!pAddress)
    {
        FP_ERROR("Failed to find address for {}", pchName ? pchName : "(null)");
        return ISource2ModMemory();
    }

    return ISource2ModMemory(pAddress);
}

const char* CGameConfig::GetPatch(const char* pchName)
{
    auto it = m_umPatches.find(pchName);
    if (it == m_umPatches.end())
    {
        return nullptr;
    }
    return it->second.c_str();
}

int CGameConfig::GetOffset(const char* pchName)
{
    auto it = m_umOffsets.find(pchName);
    if (it == m_umOffsets.end())
    {
        return -1;
    }
    return it->second;
}
