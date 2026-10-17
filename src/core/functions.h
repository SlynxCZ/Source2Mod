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
 * As a special exception, Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl gives
 * you permission to link the code of this program (as well as its derivative
 * works) to "Counter-Strike 2," "Source 2," "Steam," and any Game MODs or
 * server software running on software by Valve Corporation. You must obey the
 * GNU General Public License in all respects for all other code used.
 *
 * Additionally, this exception applies to all derivative works unless
 * otherwise stated in LICENSE.txt.
 *
 * Authors:
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */

#pragma once

#include <source2mod/ISource2ModFunctions.h>

#include <memory>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace functions
{
    /// One game function: gamedata-described (Find) or plugin-described (Create).
    class CFunction final : public ISource2ModFunction
    {
    public:
        const char* GetName() const override { return name.c_str(); }
        Source2ModType GetReturnType() const override { return returnType; }
        int GetParamCount() const override { return static_cast<int>(params.size()); }
        Source2ModType GetParamType(int index) const override;
        bool IsVirtual() const override { return vtableIndex >= 0; }
        bool IsMember() const override { return member; }
        void* GetAddress(void* pThis = nullptr, bool bypassHooks = false) const override;
        bool CheckCall(Source2ModType ret, const Source2ModType* types, int count) const override;

        std::string name;
        PluginId owner = 0;             // 0: the core's (Find)
        void* address = nullptr;        // null for a virtual
        void** vtable = nullptr;        // a virtual through one class's vtable
        int vtableIndex = -1;
        bool member = false;
        Source2ModType returnType = Source2ModType::Void;
        std::vector<Source2ModType> params;

    private:
        // Call shapes already reported as not matching, so a call in a loop
        // does not flood the console.
        mutable std::unordered_set<std::string> reported_;
    };

    class FunctionsManager final : public ISource2ModFunctions
    {
    public:
        ISource2ModFunction* Find(const char* name) override;
        ISource2ModFunction* Create(PluginId owner, void* address, void** vtable, int vtableIndex, bool isMember,
                                    Source2ModType returnType, const Source2ModType* params, int count) override;
        void Destroy(ISource2ModFunction* function) override;

        /// What a gamedata "function" names a type, false for an unknown one.
        static bool ParseType(const std::string& text, Source2ModType& out);
        static const char* TypeName(Source2ModType type);

        void RemoveAllForPlugin(PluginId id);

    private:
        // Find() results, misses included (null), so a missing entry is
        // reported once.
        std::unordered_map<std::string, std::unique_ptr<CFunction>> byName_;
        std::vector<std::unique_ptr<CFunction>> created_;
    };

    extern FunctionsManager functionsManager;
}
