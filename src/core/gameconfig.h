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
#include <dynlibutils/module.hpp>
#include <keyvalues.h>
#include <nlohmann/json.hpp>
#include <source2mod/ISource2ModGameConfig.h>

#include <string>
#include <unordered_map>

#undef snprintf

class CGameConfig : public ISource2ModGameConfig
{
public:
    using json = nlohmann::json;
    CGameConfig(const std::string& path);
    ~CGameConfig();

    bool LoadSingle(const std::string& path, char* conf_error, int conf_error_size);
    bool InitAll(const std::string& folder, char* conf_error, int conf_error_size);
    const std::string GetPath();
    const char* GetLibrary(const char* pchName) override;
    const char* GetSignature(const char* pchName) override;
    const char* GetSymbol(const char* pchName) override;
    const char* GetPatch(const char* pchName) override;

public:
    /// Whether the gamedata entry is a symbol (an "@"-prefixed name) rather
    /// than a byte pattern.
    bool IsSymbol(const char* pchName) override;

    /// The module named by the entry's "library" field, loaded on first use.
    /// Null when the entry names no library, or one that is not loaded.
    DynLibUtils::CModule* GetModule(const char* pchName);

    /// Resolves a gamedata entry to an address: by exported symbol when the
    /// entry is "@"-prefixed, by pattern scan otherwise.
    ISource2ModMemory ResolveSignature(const char* pchName) override;
    int GetOffset(const char* pchName) override;

private:
    std::string m_sPath;
    // use Valve KeyValues in the future.
    // since we'd better make '\' easier.
    json m_json;
    std::unordered_map<std::string, int> m_umOffsets;
    std::unordered_map<std::string, std::string> m_umSignatures;
    std::unordered_map<std::string, void*> m_umAddresses;
    std::unordered_map<std::string, std::string> m_umLibraries;
    std::unordered_map<std::string, std::string> m_umPatches;

    /// Modules loaded on demand by GetModule, keyed by library name.
    std::unordered_map<std::string, DynLibUtils::CModule> m_umModules;
};
