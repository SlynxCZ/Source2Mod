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

#include "hooks.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/*
 * HookPoint::Function: a hook on any ISource2ModFunction, its signature
 * described at runtime (the function's types). One KHook per function (and
 * vtable, for a virtual), its handlers -- C++ and SourcePawn alike -- in a
 * hooks::PointList with their targets and filters.
 *
 * KHook calls a hook's callbacks with the hooked function's own arguments;
 * the callbacks here are generic over registers instead of over a C++
 * signature (SysV: six integer and eight SSE registers, then eight stack
 * slots; Windows x64: four positions each integer or SSE, then stack slots),
 * and the function's types say which register holds which argument.
 */
namespace dynamichooks
{
#if defined(_WIN32)
    constexpr int kSlots = 14;
#else
    constexpr int kSlots = 22;   // 6 integer + 8 SSE + 8 stack
#endif

    /// The argument registers of one call, as the callbacks got them.
    struct Regs
    {
        uint64_t s[kSlots];
    };

    class DynHook;

    /// FunctionContext over one call: the registers, the return value. The
    /// SourcePawn natives read it through the core-only part below.
    class CallFrame final : public FunctionContext
    {
    public:
        CallFrame(DynHook* hook, const Regs& regs, bool post, uint64_t ret);

        DynHook* hook;
        Regs regs;
        bool post;
        uint64_t ret;
        bool retSet = false;
        bool changed = false;
        /// Strings a handler put in a parameter: alive until the call is over.
        std::vector<std::unique_ptr<std::string>> strings;

        Source2ModType TypeOf(int index) const;
        int Count() const;
        uint64_t Bits(int index) const;
        void SetBits(int index, uint64_t bits);
        const char* KeepString(std::string text);
        /// The original now, past every hook, with the current arguments; its
        /// value becomes the return value. false when it cannot be reached.
        bool CallOriginalNow(uint64_t* value);

    protected:
        void ReadArg(int index, void* out, size_t size) const override;
        void WriteArg(int index, const void* in, size_t size) override;
        void ReadReturn(void* out, size_t size) const override;
        void WriteReturn(const void* in, size_t size) override;
        void CallOriginalRaw(void* out, size_t size) override;
    };

    class DynamicHooksManager final
    {
    public:
        Source2ModHookId Add(ISource2ModFunction* function, PluginId owner, HookHandler<FunctionContext> handler, bool post,
                             const HookTarget& target, const HookFilter& filter);
        bool Remove(Source2ModHookId id);
        void RemoveAllForPlugin(PluginId id);
        void RemoveInstance(const void* instance);

        /// The function is about to be freed: its hooks go now.
        void ForgetFunction(ISource2ModFunction* function);

        /// Takes out the KHooks nobody listens to any more.
        void Tick();
        void Shutdown();

    private:
        // (function, vtable) -> its hook; a function on the vtables of
        // several classes has one per vtable.
        std::map<std::pair<ISource2ModFunction*, void**>, DynHook*> m_hooks;
    };

    extern DynamicHooksManager dynamicHooksManager;
}
