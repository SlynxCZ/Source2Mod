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
#include "module.h"

#include "plugin.h"
// plugin.h brings in windows.h, whose GetModuleHandle macro would rename
// Source2ModModule::GetModuleHandle below.
#undef GetModuleHandle

#include <algorithm>
#include <cctype>

/// Every spelling of the server module: "server", "server.dll", "server.so",
/// "libserver", "libserver.so", any case, with or without a directory.
static bool IsServerModuleName(std::string_view name)
{
    if (auto slash = name.find_last_of("/\\"); slash != std::string_view::npos)
        name.remove_prefix(slash + 1);

    std::string bare(name);
    std::transform(bare.begin(), bare.end(), bare.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (bare.ends_with(".dll"))
        bare.resize(bare.size() - 4);
    else if (bare.ends_with(".so"))
        bare.resize(bare.size() - 3);
    if (bare.starts_with("lib"))
        bare.erase(0, 3);

    return bare == "server";
}

bool InitModuleFromName(DynLibUtils::CModule& module, std::string_view name, bool extension)
{
    if (IsServerModuleName(name))
    {
        // Unsynthesized: the game's own factory, not metamod's.
        auto factory = g_SMAPI->GetServerFactory(false);
        return factory && module.InitFromMemory(DynLibUtils::CMemory(reinterpret_cast<void*>(factory)));
    }

    return module.InitFromName(name, extension);
}

Source2ModModule::Source2ModModule(const char* name)
{
    InitFromName(name);
}

Source2ModModule::Source2ModModule(uintptr_t ptr)
{
    InitFromMemory(ptr);
}

bool Source2ModModule::InitFromName(const char* name, bool extension)
{
    m_cachedName.clear();
    return name && InitModuleFromName(m_module, name, extension);
}

bool Source2ModModule::InitFromMemory(uintptr_t ptr)
{
    m_cachedName.clear();
    return m_module.InitFromMemory(DynLibUtils::CMemory(ptr));
}

ISource2ModMemory Source2ModModule::FindPattern(const char* pattern, ISource2ModMemory startAddress) const
{
    return ISource2ModMemory(m_module.FindPattern(DynLibUtils::ParsePattern(pattern), DynLibUtils::CMemory(startAddress.GetAddr())).GetPtr());
}

ISource2ModMemory Source2ModModule::FindPatternInSection(const char* pattern, const char* section, ISource2ModMemory startAddress) const
{
    return ISource2ModMemory(m_module.FindPattern(DynLibUtils::ParsePattern(pattern), DynLibUtils::CMemory(startAddress.GetAddr()), m_module.GetSectionByName(section)).GetPtr());
}

ISource2ModMemory Source2ModModule::GetVirtualTableByName(const char* name, bool decorated) const
{
    return ISource2ModMemory(m_module.GetVirtualTableByName(name, decorated).GetPtr());
}

ISource2ModMemory Source2ModModule::GetVirtualTableByOffset(const char* name, std::ptrdiff_t offset, bool decorated) const
{
    if (!name)
        return ISource2ModMemory();

    return ISource2ModMemory(m_module.GetVirtualTableByOffset(name, offset, decorated).GetPtr());
}

ISource2ModMemory Source2ModModule::GetVirtualTableByBase(const char* name, const char* baseName, bool decorated) const
{
    if (!name || !baseName)
        return ISource2ModMemory();

    return ISource2ModMemory(m_module.GetVirtualTableByBase(name, baseName, decorated).GetPtr());
}

ISource2ModMemory Source2ModModule::GetFunctionByName(const char* name) const
{
    return ISource2ModMemory(m_module.GetFunctionByName(name).GetPtr());
}

ISource2ModModule::SectionInfo Source2ModModule::GetSectionByName(const char* name) const
{
    const auto* sec = m_module.GetSectionByName(name);
    if (!sec) return { 0, 0 };
    return { reinterpret_cast<uintptr_t>(sec->GetPtr()), sec->m_nSectionSize };
}

void* Source2ModModule::GetModuleHandle() const
{
    return m_module.GetHandle();
}

ISource2ModMemory Source2ModModule::GetModuleBase() const
{
    return ISource2ModMemory(m_module.GetBase().GetPtr());
}

const char* Source2ModModule::GetModulePath() const
{
    return m_module.GetPath().data();
}

const char* Source2ModModule::GetModuleName() const
{
    if (m_cachedName.empty())
        m_cachedName = m_module.GetName();
    return m_cachedName.c_str();
}
