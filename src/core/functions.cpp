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

/*
 * ISource2ModFunctions: game functions described in gamedata ("function"
 * objects, see ISource2ModFunctions.h), shared by C++ (Call<Sig>) and
 * SourcePawn (Source2ModFunction.Find, natives_gameconfig.cpp).
 */
#include "functions.h"

#include "gameconfig.h"
#include "shared.h"
#include "utils/log.h"

#include <khook.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace functions
{
    FunctionsManager functionsManager;

    namespace
    {
        struct TypeEntry
        {
            const char* name;
            Source2ModType type;
        };

        constexpr TypeEntry kTypes[] = {
            { "void", Source2ModType::Void },
            { "bool", Source2ModType::Bool },
            { "int8", Source2ModType::Int8 },
            { "int16", Source2ModType::Int16 },
            { "int32", Source2ModType::Int32 },
            { "int64", Source2ModType::Int64 },
            { "float", Source2ModType::Float },
            { "double", Source2ModType::Double },
            { "entity", Source2ModType::Entity },
            { "object", Source2ModType::Object },
            { "string", Source2ModType::String },
            { "vector", Source2ModType::Vector },
            { "vector_value", Source2ModType::VectorValue },
            { "pointer", Source2ModType::Pointer },
            { "buffer_string", Source2ModType::BufferString },
            { "command", Source2ModType::Command },
            { "int_ref", Source2ModType::IntRef },
            { "bool_ref", Source2ModType::BoolRef },
            { "float_ref", Source2ModType::FloatRef },
            // Aliases: what the C++ type is called.
            { "uint8", Source2ModType::Int8 },
            { "uint16", Source2ModType::Int16 },
            { "uint32", Source2ModType::Int32 },
            { "uint64", Source2ModType::Int64 },
            { "int", Source2ModType::Int32 },
            { "enum", Source2ModType::Int32 },
            { "qangle", Source2ModType::Vector },
        };

        bool IsPointerLike(Source2ModType t)
        {
            return t == Source2ModType::Entity || t == Source2ModType::Object || t == Source2ModType::Pointer;
        }

        // Whether what the C++ call says (`cpp`, from Call<Sig>) fits what the
        // gamedata says (`desc`). Any C++ pointer fits an entity / object /
        // pointer, and a pointer in the gamedata takes any C++ pointer: the
        // ABI passes them all alike.
        bool Fits(Source2ModType cpp, Source2ModType desc)
        {
            if (cpp == desc)
                return true;
            if (cpp == Source2ModType::Pointer)
                return IsPointerLike(desc) || desc == Source2ModType::String || desc == Source2ModType::BufferString
                    || desc == Source2ModType::IntRef || desc == Source2ModType::BoolRef || desc == Source2ModType::FloatRef
                    || desc == Source2ModType::Vector || desc == Source2ModType::Command;
            if (desc == Source2ModType::Pointer)
                return cpp != Source2ModType::Void && cpp != Source2ModType::VectorValue && cpp != Source2ModType::Float
                    && cpp != Source2ModType::Double && cpp != Source2ModType::Bool && cpp != Source2ModType::Int8
                    && cpp != Source2ModType::Int16 && cpp != Source2ModType::Int32 && cpp != Source2ModType::Int64;
            return false;
        }

        bool ParseList(const nlohmann::json& list, std::vector<Source2ModType>& out, std::string& error)
        {
            if (!list.is_array())
            {
                error = "\"params\" is not a list";
                return false;
            }
            for (const auto& item : list)
            {
                Source2ModType t;
                if (!item.is_string() || !FunctionsManager::ParseType(item.get<std::string>(), t) || t == Source2ModType::Void)
                {
                    error = "unknown parameter type " + item.dump();
                    return false;
                }
                out.push_back(t);
            }
            return true;
        }

        // A "function" object into `fn` (member, return, params).
        bool ParseDescriptor(const nlohmann::json& d, CFunction& fn, std::string& error)
        {
#if defined(_WIN32)
            constexpr const char* platform = "windows";
#else
            constexpr const char* platform = "linux";
#endif
            if (auto it = d.find("this"); it != d.end())
            {
                Source2ModType t;
                if (!it->is_string() || !FunctionsManager::ParseType(it->get<std::string>(), t) || !IsPointerLike(t))
                {
                    error = "\"this\" must be entity, object or pointer";
                    return false;
                }
                fn.member = true;
                fn.params.push_back(t);
            }

            fn.returnType = Source2ModType::Void;
            if (auto it = d.find("return"); it != d.end())
            {
                if (!it->is_string() || !FunctionsManager::ParseType(it->get<std::string>(), fn.returnType))
                {
                    error = "unknown return type " + it->dump();
                    return false;
                }
            }

            if (auto it = d.find("params"); it != d.end())
            {
                if (it->is_object())
                {
                    auto p = it->find(platform);
                    if (p == it->end())
                    {
                        error = std::string("\"params\" has no ") + platform + " list";
                        return false;
                    }
                    return ParseList(*p, fn.params, error);
                }
                return ParseList(*it, fn.params, error);
            }
            return true;
        }
    }

    // ---- CFunction ------------------------------------------------------------------

    Source2ModType CFunction::GetParamType(int index) const
    {
        return index >= 0 && index < static_cast<int>(params.size()) ? params[index] : Source2ModType::Void;
    }

    void* CFunction::GetAddress(void* pThis, bool bypassHooks) const
    {
        if (vtableIndex >= 0)
        {
            void** table = vtable;
            if (!table)
            {
                if (!pThis)
                    return nullptr;
                table = *static_cast<void***>(pThis);
            }
            return bypassHooks ? KHook::FindOriginal(KHook::FindOriginalVirtual(table, vtableIndex)) : table[vtableIndex];
        }
        if (!address)
            return nullptr;
        return bypassHooks ? KHook::FindOriginal(address) : address;
    }

    bool CFunction::CheckCall(Source2ModType ret, const Source2ModType* types, int count) const
    {
        std::string problem;
        if (count != static_cast<int>(params.size()))
            problem = "takes " + std::to_string(params.size()) + " parameter(s), the call passes " + std::to_string(count);
        else
        {
            for (int i = 0; i < count && problem.empty(); i++)
                if (!Fits(types[i], params[i]))
                    problem = "parameter " + std::to_string(i + 1) + " is " + FunctionsManager::TypeName(params[i]) + ", the call passes "
                        + FunctionsManager::TypeName(types[i]);
            // Ignoring a result is fine, except a struct returned by value:
            // where it goes depends on the caller knowing about it.
            const bool ignored = ret == Source2ModType::Void && returnType != Source2ModType::VectorValue;
            if (problem.empty() && !ignored && !Fits(ret, returnType))
                problem = std::string("returns ") + FunctionsManager::TypeName(returnType) + ", the call expects " + FunctionsManager::TypeName(ret);
        }
        if (problem.empty())
            return true;

        std::string shape = std::to_string(static_cast<int>(ret));
        for (int i = 0; i < count; i++)
            shape += "," + std::to_string(static_cast<int>(types[i]));
        if (reported_.insert(shape).second)
            FP_ERROR("Function {}: {}; not called", name.empty() ? "(created)" : name, problem);
        return false;
    }

    // ---- FunctionsManager -----------------------------------------------------------

    bool FunctionsManager::ParseType(const std::string& text, Source2ModType& out)
    {
        std::string lower = text;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
        for (const auto& t : kTypes)
        {
            if (lower == t.name)
            {
                out = t.type;
                return true;
            }
        }
        return false;
    }

    const char* FunctionsManager::TypeName(Source2ModType type)
    {
        for (const auto& t : kTypes)
            if (t.type == type)
                return t.name;
        return "?";
    }

    ISource2ModFunction* FunctionsManager::Find(const char* name)
    {
        if (!name || !*name)
            return nullptr;
        if (auto it = byName_.find(name); it != byName_.end())
            return it->second.get();

        auto& slot = byName_[name];
        CGameConfig* config = shared::g_pGameConfig;
        const nlohmann::json* d = config ? config->GetFunction(name) : nullptr;
        if (!d)
        {
            FP_WARN("Function {}: no gamedata entry with a \"function\" object", name);
            return nullptr;
        }

        auto fn = std::make_unique<CFunction>();
        fn->name = name;
        std::string error;
        if (!ParseDescriptor(*d, *fn, error))
        {
            FP_ERROR("Function {}: {}", name, error);
            return nullptr;
        }

        if (config->HasSignature(name))
        {
            fn->address = reinterpret_cast<void*>(config->ResolveSignature(name).GetAddr());
            if (!fn->address)
            {
                FP_ERROR("Function {}: the signature does not match the running game", name);
                return nullptr;
            }
        }
        else
        {
            const int offset = config->GetOffset(name);
            if (offset < 0)
            {
                FP_ERROR("Function {}: the entry has neither a signature nor an offset for this platform", name);
                return nullptr;
            }
            if (!fn->member)
            {
                FP_ERROR("Function {}: a virtual (\"offsets\") needs \"this\"", name);
                return nullptr;
            }
            fn->vtableIndex = offset;
        }

        slot = std::move(fn);
        return slot.get();
    }

    ISource2ModFunction* FunctionsManager::Create(PluginId owner, void* address, void** vtable, int vtableIndex, bool isMember,
                                                  Source2ModType returnType, const Source2ModType* params, int count)
    {
        if (!address && vtableIndex < 0)
            return nullptr;
        if (vtableIndex >= 0 && count < 1)
            return nullptr;     // a virtual needs `this`
        auto fn = std::make_unique<CFunction>();
        fn->owner = owner;
        fn->address = vtableIndex >= 0 ? nullptr : address;
        fn->vtable = vtable;
        fn->vtableIndex = vtableIndex;
        fn->member = isMember || vtableIndex >= 0;
        fn->returnType = returnType;
        if (params && count > 0)
            fn->params.assign(params, params + count);
        created_.push_back(std::move(fn));
        return created_.back().get();
    }

    void FunctionsManager::Destroy(ISource2ModFunction* function)
    {
        created_.erase(std::remove_if(created_.begin(), created_.end(),
                                      [function](const std::unique_ptr<CFunction>& f) { return f.get() == function; }),
                       created_.end());
    }

    void FunctionsManager::RemoveAllForPlugin(PluginId id)
    {
        created_.erase(std::remove_if(created_.begin(), created_.end(),
                                      [id](const std::unique_ptr<CFunction>& f) { return f->owner == id; }),
                       created_.end());
    }
}
