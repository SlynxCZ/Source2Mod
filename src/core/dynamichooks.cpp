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

#include "dynamichooks.h"

#include "functions.h"
#include "module.h"
#include "utils/log.h"

#include <mathlib/vector.h>

#include <khook.hpp>

#include <algorithm>
#include <cstring>
#include <type_traits>

namespace dynamichooks
{
    DynamicHooksManager dynamicHooksManager;

    namespace
    {
        bool IsSse(Source2ModType t)
        {
            return t == Source2ModType::Float || t == Source2ModType::Double;
        }

        template <typename R>
        R FromBits(uint64_t bits)
        {
            R r{};
            memcpy(&r, &bits, sizeof(R) < sizeof(bits) ? sizeof(R) : sizeof(bits));
            return r;
        }

        template <typename R>
        uint64_t ToBits(R r)
        {
            uint64_t bits = 0;
            memcpy(&bits, &r, sizeof(R) < sizeof(bits) ? sizeof(R) : sizeof(bits));
            return bits;
        }

        void SaveReturn(Action action, uint64_t* value, bool hasValue, bool original)
        {
            if (hasValue)
                KHook::SaveReturnValue(action, value, sizeof(uint64_t), reinterpret_cast<void*>(KHook::init_operator<uint64_t>),
                                       reinterpret_cast<void*>(KHook::deinit_operator<uint64_t>), original);
            else
                KHook::SaveReturnValue(action, nullptr, 0, nullptr, nullptr, original);
        }
    }

    /// One KHook: a function (on one vtable, for a virtual) and its handlers.
    class DynHook
    {
    public:
        functions::CFunction* function = nullptr;     // null once the function is gone (ForgetFunction)
        std::vector<Source2ModType> params;          // the function's types, kept: the hook may outlive it
        Source2ModType returnType = Source2ModType::Void;
        void* address = nullptr;
        void** vtable = nullptr;
        int index = -1;
        std::vector<int> slot;      // parameter i lives in Regs::s[slot[i]] (a Vector by value on SysV: and the next)
        hooks::PointList<FunctionContext> list;
        KHook::HookID_t id = KHook::INVALID_HOOK;
        bool removing = false;      // RemoveHook asked, KHook not done yet
        bool zombie = false;        // the manager let go: delete once removed
        uint64_t (*invoke)(void* fn, const Regs& regs) = nullptr;   // a call in this hook's register shape
        std::string name;

        bool HasReturn() const { return returnType != Source2ModType::Void; }

        void* Original() const
        {
            return vtable ? KHook::FindOriginal(KHook::FindOriginalVirtual(vtable, index)) : KHook::FindOriginal(address);
        }
    };

    namespace
    {
        void OnRemoved(KHook::HookID_t, void* ctx)
        {
            auto* hook = static_cast<DynHook*>(ctx);
            hook->removing = false;
            if (hook->zombie)
                delete hook;
        }

        void BeginRemove(DynHook* hook)
        {
            if (hook->id == KHook::INVALID_HOOK)
                return;
            const KHook::HookID_t id = hook->id;
            hook->id = KHook::INVALID_HOOK;
            hook->removing = true;
            KHook::RemoveHook(id, true, OnRemoved, hook);
        }

        // pre / post: the handlers, then the action for KHook.
        uint64_t Handle(const Regs& regs, bool post)
        {
            auto* hook = static_cast<DynHook*>(KHook::GetContextPtr());
            const bool hasValue = hook->HasReturn();

            uint64_t original = 0;
            if (post && hasValue)
                if (void* p = KHook::GetOriginalValuePtr())
                    original = *static_cast<uint64_t*>(p);

            CallFrame frame(hook, regs, post, original);

            Action action = Action::Ignore;
            if (!hook->zombie && hook->list.Has(post))
            {
                // The object is `this` (a free function's first argument); an
                // entity one also answers HookFilter::designerName.
                hooks::Subject subject;
                if (!hook->params.empty())
                {
                    subject.object = reinterpret_cast<const void*>(frame.Bits(0));
                    if (hook->params[0] == Source2ModType::Entity)
                        subject.entity = reinterpret_cast<CEntityInstance*>(frame.Bits(0));
                }
                action = hook->list.Dispatch(frame, post, subject, hook->name.c_str());
            }

            uint64_t value = frame.ret;

            // Changed arguments go on the way KHook::Recall sends them: the
            // detour is entered again with the new registers and carries on
            // from the next hook, so every later hook and the original get
            // them. The recall shares this call's loop: the original runs once,
            // inside it, unless the action saved here is Supersede. Override
            // with new arguments alone means "call the original with them".
            if (!post && frame.changed && hook->invoke)
            {
                if (action == Action::Override && !frame.retSet)
                    action = Action::Ignore;
                void* again = hasValue
                    ? KHook::DoRecall(action, &value, sizeof(value), reinterpret_cast<void*>(KHook::init_operator<uint64_t>),
                                      reinterpret_cast<void*>(KHook::deinit_operator<uint64_t>))
                    : KHook::DoRecall(action, nullptr, 0, nullptr, nullptr);
                hook->invoke(again, frame.regs);
            }

            // Override / Supersede without a value set: the current one stands.
            if (action != Action::Ignore && hasValue && !frame.retSet && !post)
                action = action == Action::Supersede ? Action::Supersede : Action::Ignore;

            SaveReturn(action, &value, hasValue, false);
            return value;
        }

        // make-return and call-original are the FIRST hook's on the function,
        // and KHook's context pointer is not that hook there: neither may read
        // the DynHook. The value is always kept as eight bytes.
        uint64_t MakeReturn()
        {
            uint64_t value = 0;
            if (void* current = KHook::GetCurrentValuePtr(true))
                value = *static_cast<uint64_t*>(current);
            KHook::DestroyReturnValue();
            return value;
        }

        struct CallbackSet
        {
            void* pre;
            void* post;
            void* ret;
            void* original;
            uint64_t (*invoke)(void*, const Regs&);
        };

#if defined(_WIN32)
        template <bool F>
        using Pos = std::conditional_t<F, double, uint64_t>;

        template <bool F>
        uint64_t Bits(Pos<F> v)
        {
            if constexpr (F)
                return ToBits(v);
            else
                return v;
        }

        template <bool F>
        Pos<F> Unbits(uint64_t v)
        {
            if constexpr (F)
                return FromBits<double>(v);
            else
                return v;
        }

#define S2M_ARGS Pos<F0> a0, Pos<F1> a1, Pos<F2> a2, Pos<F3> a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8, uint64_t a9, uint64_t a10, uint64_t a11, uint64_t a12, uint64_t a13
#define S2M_REGS Regs regs{ { Bits<F0>(a0), Bits<F1>(a1), Bits<F2>(a2), Bits<F3>(a3), a4, a5, a6, a7, a8, a9, a10, a11, a12, a13 } }

        template <typename R, bool F0, bool F1, bool F2, bool F3>
        struct Callbacks
        {
            static R Out(uint64_t v) { return FromBits<R>(v); }

            static R Pre(S2M_ARGS) { S2M_REGS; return Out(Handle(regs, false)); }
            static R Post(S2M_ARGS) { S2M_REGS; return Out(Handle(regs, true)); }
            static R Return(S2M_ARGS) { return Out(MakeReturn()); }

            static uint64_t Invoke(void* fn, const Regs& regs)
            {
                using Fn = R (*)(Pos<F0>, Pos<F1>, Pos<F2>, Pos<F3>, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
                const uint64_t* s = regs.s;
                return ToBits(reinterpret_cast<Fn>(fn)(Unbits<F0>(s[0]), Unbits<F1>(s[1]), Unbits<F2>(s[2]), Unbits<F3>(s[3]),
                                                        s[4], s[5], s[6], s[7], s[8], s[9], s[10], s[11], s[12], s[13]));
            }

            static R CallOriginal(S2M_ARGS)
            {
                S2M_REGS;
                uint64_t value = Invoke(KHook::GetOriginalFunction(), regs);
                SaveReturn(Action::Ignore, &value, true, true);
                return Out(value);
            }
        };
#undef S2M_ARGS
#undef S2M_REGS

        template <typename R, int M>
        CallbackSet SetFor()
        {
            using C = Callbacks<R, (M & 1) != 0, (M & 2) != 0, (M & 4) != 0, (M & 8) != 0>;
            return { reinterpret_cast<void*>(&C::Pre), reinterpret_cast<void*>(&C::Post), reinterpret_cast<void*>(&C::Return),
                     reinterpret_cast<void*>(&C::CallOriginal), &C::Invoke };
        }

        template <typename R>
        CallbackSet Pick(int mask)
        {
            switch (mask)
            {
#define S2M_CASE(m) case m: return SetFor<R, m>();
                S2M_CASE(0) S2M_CASE(1) S2M_CASE(2) S2M_CASE(3) S2M_CASE(4) S2M_CASE(5) S2M_CASE(6) S2M_CASE(7)
                S2M_CASE(8) S2M_CASE(9) S2M_CASE(10) S2M_CASE(11) S2M_CASE(12) S2M_CASE(13) S2M_CASE(14) S2M_CASE(15)
#undef S2M_CASE
            }
            return SetFor<R, 0>();
        }

        // One position per parameter, in order; a Vector by value is a hidden
        // pointer here, an integer position like any other.
        bool Layout(DynHook* hook, int* mask, std::string& error)
        {
            const auto& params = hook->params;
            if (params.size() > static_cast<size_t>(kSlots))
            {
                error = "too many parameters (at most 14)";
                return false;
            }
            *mask = 0;
            hook->slot.clear();
            for (size_t i = 0; i < params.size(); i++)
            {
                if (IsSse(params[i]) && i < 4)
                    *mask |= 1 << i;
                hook->slot.push_back(static_cast<int>(i));
            }
            return true;
        }

        unsigned StackSize(const DynHook* hook)
        {
            size_t args = hook->params.size() + (hook->HasReturn() ? 1 : 0);
            return static_cast<unsigned>((args <= 4 ? 0 : (args - 4)) * 8 + 32);
        }
#else
        // SysV: s[0..5] the integer registers, s[6..13] the SSE ones,
        // s[14..21] the first eight stack slots.
#define S2M_ARGS uint64_t i0, uint64_t i1, uint64_t i2, uint64_t i3, uint64_t i4, uint64_t i5, double f0, double f1, double f2, double f3, double f4, double f5, double f6, double f7, \
                 uint64_t k0, uint64_t k1, uint64_t k2, uint64_t k3, uint64_t k4, uint64_t k5, uint64_t k6, uint64_t k7
#define S2M_REGS Regs regs{ { i0, i1, i2, i3, i4, i5, ToBits(f0), ToBits(f1), ToBits(f2), ToBits(f3), ToBits(f4), ToBits(f5), ToBits(f6), ToBits(f7), \
                              k0, k1, k2, k3, k4, k5, k6, k7 } }

        template <typename R>
        struct Callbacks
        {
            static R Out(uint64_t v) { return FromBits<R>(v); }

            static R Pre(S2M_ARGS) { S2M_REGS; return Out(Handle(regs, false)); }
            static R Post(S2M_ARGS) { S2M_REGS; return Out(Handle(regs, true)); }
            static R Return(S2M_ARGS) { return Out(MakeReturn()); }

            static uint64_t Invoke(void* fn, const Regs& regs)
            {
                using Fn = R (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, double, double, double, double, double, double, double, double,
                                 uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
                const uint64_t* s = regs.s;
                return ToBits(reinterpret_cast<Fn>(fn)(s[0], s[1], s[2], s[3], s[4], s[5], FromBits<double>(s[6]), FromBits<double>(s[7]),
                                                        FromBits<double>(s[8]), FromBits<double>(s[9]), FromBits<double>(s[10]),
                                                        FromBits<double>(s[11]), FromBits<double>(s[12]), FromBits<double>(s[13]),
                                                        s[14], s[15], s[16], s[17], s[18], s[19], s[20], s[21]));
            }

            static R CallOriginal(S2M_ARGS)
            {
                S2M_REGS;
                uint64_t value = Invoke(KHook::GetOriginalFunction(), regs);
                SaveReturn(Action::Ignore, &value, true, true);
                return Out(value);
            }
        };
#undef S2M_ARGS
#undef S2M_REGS

        template <typename R>
        CallbackSet Pick(int)
        {
            using C = Callbacks<R>;
            return { reinterpret_cast<void*>(&C::Pre), reinterpret_cast<void*>(&C::Post), reinterpret_cast<void*>(&C::Return),
                     reinterpret_cast<void*>(&C::CallOriginal), &C::Invoke };
        }

        // By class: integer parameters take s[0..5] in order, SSE ones s[6..13];
        // what overflows its class takes the next stack slot, s[14..21]. A
        // Vector by value is two SSE eightbytes (x and y packed, then z): two
        // SSE registers, or -- when two are not left -- two stack slots.
        bool Layout(DynHook* hook, int* mask, std::string& error)
        {
            *mask = 0;
            hook->slot.clear();
            int ni = 0, nf = 0, ns = 0;
            for (Source2ModType t : hook->params)
            {
                int slot;
                if (t == Source2ModType::VectorValue)
                {
                    if (nf + 2 <= 8)
                    {
                        slot = 6 + nf;
                        nf += 2;
                    }
                    else if (ns + 2 <= 8)
                    {
                        slot = 14 + ns;
                        ns += 2;
                    }
                    else
                    {
                        error = "too many parameters";
                        return false;
                    }
                }
                else if (IsSse(t) && nf < 8)
                    slot = 6 + nf++;
                else if (!IsSse(t) && ni < 6)
                    slot = ni++;
                else if (ns < 8)
                    slot = 14 + ns++;
                else
                {
                    error = "too many parameters (6 integer and 8 floating-point in registers, then 8 on the stack)";
                    return false;
                }
                hook->slot.push_back(slot);
            }
            return true;
        }

        // The bytes of stack arguments KHook copies for each callback: more
        // than the stack arguments need (one slot per parameter, plus one).
        unsigned StackSize(const DynHook* hook)
        {
            return static_cast<unsigned>((hook->HasReturn() ? 8 : 0) + 8 * (hook->params.size() + 1));
        }
#endif

        bool ValidReturn(Source2ModType t)
        {
            // What fits RAX or XMM0: not a struct by value, not a reference.
            return t != Source2ModType::VectorValue && t != Source2ModType::Command && t != Source2ModType::BufferString;
        }
    }

    // ---- CallFrame ------------------------------------------------------------------

    CallFrame::CallFrame(DynHook* h, const Regs& r, bool p, uint64_t value)
        : hook(h), regs(r), post(p), ret(value)
    {
        function_ = h->function;
    }

    Source2ModType CallFrame::TypeOf(int index) const
    {
        return index >= 0 && index < Count() ? hook->params[index] : Source2ModType::Void;
    }

    int CallFrame::Count() const
    {
        return static_cast<int>(hook->params.size());
    }

    uint64_t CallFrame::Bits(int index) const
    {
        return index >= 0 && index < Count() ? regs.s[hook->slot[index]] : 0;
    }

    void CallFrame::SetBits(int index, uint64_t bits)
    {
        if (index < 0 || index >= Count())
            return;
        regs.s[hook->slot[index]] = bits;
        changed = true;
    }

    const char* CallFrame::KeepString(std::string text)
    {
        strings.push_back(std::make_unique<std::string>(std::move(text)));
        return strings.back()->c_str();
    }

    bool CallFrame::CallOriginalNow(uint64_t* value)
    {
        void* original = hook->Original();
        if (!original || !hook->invoke)
            return false;
        const uint64_t v = hook->invoke(original, regs);
        if (hook->HasReturn())
        {
            ret = v;
            retSet = true;
        }
        if (value)
            *value = v;
        return true;
    }

    void CallFrame::ReadArg(int index, void* out, size_t size) const
    {
        if (index < 0 || index >= Count())
            return;
        const int s = hook->slot[index];
        if (TypeOf(index) == Source2ModType::VectorValue && size == sizeof(Vector))
        {
#if defined(_WIN32)
            if (const auto* v = reinterpret_cast<const Vector*>(regs.s[s]))
                memcpy(out, v, sizeof(Vector));
#else
            float xyz[3];
            memcpy(&xyz[0], &regs.s[s], sizeof(float) * 2);
            memcpy(&xyz[2], &regs.s[s + 1], sizeof(float));
            memcpy(out, xyz, sizeof(xyz));
#endif
            return;
        }
        memcpy(out, &regs.s[s], size < sizeof(uint64_t) ? size : sizeof(uint64_t));
    }

    void CallFrame::WriteArg(int index, const void* in, size_t size)
    {
        if (index < 0 || index >= Count())
            return;
        const int s = hook->slot[index];
        if (TypeOf(index) == Source2ModType::VectorValue && size == sizeof(Vector))
        {
#if defined(_WIN32)
            // Passed by hidden reference: written where it is.
            if (auto* v = reinterpret_cast<Vector*>(regs.s[s]))
                memcpy(v, in, sizeof(Vector));
#else
            const auto* xyz = static_cast<const float*>(in);
            uint64_t xy = 0, z = 0;
            memcpy(&xy, xyz, sizeof(float) * 2);
            memcpy(&z, xyz + 2, sizeof(float));
            regs.s[s] = xy;
            regs.s[s + 1] = z;
            changed = true;
#endif
            return;
        }
        uint64_t bits = 0;
        memcpy(&bits, in, size < sizeof(bits) ? size : sizeof(bits));
        regs.s[s] = bits;
        changed = true;
    }

    void CallFrame::ReadReturn(void* out, size_t size) const
    {
        memcpy(out, &ret, size < sizeof(ret) ? size : sizeof(ret));
    }

    void CallFrame::WriteReturn(const void* in, size_t size)
    {
        uint64_t bits = 0;
        memcpy(&bits, in, size < sizeof(bits) ? size : sizeof(bits));
        ret = bits;
        retSet = true;
    }

    void CallFrame::CallOriginalRaw(void* out, size_t size)
    {
        uint64_t value = 0;
        if (CallOriginalNow(&value) && out && size)
            memcpy(out, &value, size < sizeof(value) ? size : sizeof(value));
    }

    // ---- the manager ----------------------------------------------------------------

    Source2ModHookId DynamicHooksManager::Add(ISource2ModFunction* function, PluginId owner, HookHandler<FunctionContext> handler, bool post,
                                              const HookTarget& target, const HookFilter& filter)
    {
        auto* fn = static_cast<functions::CFunction*>(function);
        const char* label = fn->name.empty() ? "(created)" : fn->name.c_str();

        if (!ValidReturn(fn->returnType))
        {
            FP_ERROR("Hook on function {}: a {} return value cannot be hooked", label, functions::FunctionsManager::TypeName(fn->returnType));
            return 0;
        }

        // Where the detour goes: the address, or a vtable slot -- the
        // function's own vtable, else the target's.
        void* address = nullptr;
        void** vtable = nullptr;
        if (fn->vtableIndex >= 0)
        {
            vtable = fn->vtable;
            if (!vtable && target.kind == HookTarget::Kind::Instance && target.instance)
                vtable = *static_cast<void***>(target.instance);
            if (!vtable && target.kind == HookTarget::Kind::Class && target.className)
                vtable = reinterpret_cast<void**>(Source2ModModule("server").GetVirtualTableByName(target.className).GetAddr());
            if (!vtable)
            {
                FP_ERROR("Hook on function {}: a virtual needs a vtable -- describe it with FromVirtualTable, or hook it on "
                         "HookTarget::Instance / HookTarget::Class (a class of libserver)", label);
                return 0;
            }
        }
        else
        {
            address = fn->address;
            if (!address)
            {
                FP_ERROR("Hook on function {}: it has no address", label);
                return 0;
            }
        }

        DynHook*& slot = m_hooks[{ function, vtable }];
        if (!slot)
        {
            auto* hook = new DynHook();
            hook->function = fn;
            hook->params = fn->params;
            hook->returnType = fn->returnType;
            hook->address = address;
            hook->vtable = vtable;
            hook->index = fn->vtableIndex;
            hook->name = std::string("Function ") + label;
            slot = hook;
        }
        DynHook* hook = slot;

        const Source2ModHookId id = hook->list.Add(owner, std::move(handler), post, target, filter);

        if (hook->id == KHook::INVALID_HOOK)
        {
            if (hook->removing)
            {
                // KHook is still taking the old detour out: a new hook object
                // takes its place, the old one goes when KHook is done.
                auto* fresh = new DynHook();
                fresh->function = fn;
                fresh->params = fn->params;
                fresh->returnType = fn->returnType;
                fresh->address = address;
                fresh->vtable = vtable;
                fresh->index = fn->vtableIndex;
                fresh->name = hook->name;
                std::swap(fresh->list, hook->list);
                hook->zombie = true;
                slot = fresh;
                hook = fresh;
            }

            int mask = 0;
            std::string error;
            if (!Layout(hook, &mask, error))
            {
                FP_ERROR("Hook on function {}: {}", label, error);
                hook->list.Remove(id);
                return 0;
            }
            const bool sse = IsSse(hook->returnType);
            const CallbackSet cb = sse ? Pick<double>(mask) : Pick<uint64_t>(mask);
            hook->invoke = cb.invoke;
            // Inserted now, so a call right after Hook() is seen. KHook only
            // try-locks the detour for that and, when the function is running
            // (a Hook() from inside its own handler), queues it to its worker
            // thread instead -- live a few milliseconds later.
            if (hook->vtable)
                hook->id = KHook::SetupVirtualHook(hook->vtable, hook->index, hook, nullptr, cb.pre, cb.post, cb.ret, cb.original, StackSize(hook), false);
            else
                hook->id = KHook::SetupHook(hook->address, hook, nullptr, cb.pre, cb.post, cb.ret, cb.original, StackSize(hook), false);
            if (hook->id == KHook::INVALID_HOOK)
            {
                FP_ERROR("Hook on function {}: KHook refused it", label);
                hook->list.Remove(id);
                return 0;
            }
        }
        return id;
    }

    bool DynamicHooksManager::Remove(Source2ModHookId id)
    {
        for (auto& [key, hook] : m_hooks)
            if (hook->list.Remove(id))
                return true;
        return false;
    }

    void DynamicHooksManager::RemoveAllForPlugin(PluginId id)
    {
        for (auto& [key, hook] : m_hooks)
            hook->list.RemoveOwner(id);
    }

    void DynamicHooksManager::RemoveInstance(const void* instance)
    {
        for (auto& [key, hook] : m_hooks)
            hook->list.RemoveInstance(instance);
    }

    void DynamicHooksManager::ForgetFunction(ISource2ModFunction* function)
    {
        for (auto it = m_hooks.begin(); it != m_hooks.end();)
        {
            if (it->first.first != function)
            {
                ++it;
                continue;
            }
            DynHook* hook = it->second;
            hook->function = nullptr;
            hook->list = hooks::PointList<FunctionContext>();
            BeginRemove(hook);
            if (hook->removing)
                hook->zombie = true;
            else
                delete hook;
            it = m_hooks.erase(it);
        }
    }

    void DynamicHooksManager::Tick()
    {
        for (auto it = m_hooks.begin(); it != m_hooks.end();)
        {
            DynHook* hook = it->second;
            if (hook->list.Empty())
            {
                BeginRemove(hook);
                if (hook->removing)
                    hook->zombie = true;
                else
                    delete hook;
                it = m_hooks.erase(it);
            }
            else
                ++it;
        }
    }

    void DynamicHooksManager::Shutdown()
    {
        for (auto& [key, hook] : m_hooks)
        {
            BeginRemove(hook);
            if (hook->removing)
                hook->zombie = true;
            else
                delete hook;
        }
        m_hooks.clear();
    }
}
