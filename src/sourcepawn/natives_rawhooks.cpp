/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * Raw hooks on game functions from SourcePawn (source2mod/hooks.inc): what
 * C++ does with KHOOK_MEMBER / KHOOK_FUNCTION / KHOOK_VIRTUAL, with the
 * signature described at runtime.
 *
 * KHook calls a hook's callbacks (pre, post, make-return, call-original)
 * with the hooked function's own arguments. The callbacks here are generic
 * over registers instead of over a C++ signature: on SysV x86-64 every
 * callback takes the six integer and eight SSE argument registers and then
 * eight stack slots (what overflows either class, in argument order; KHook
 * copies the caller's stack arguments for every callback it makes); on
 * Windows x64 the four argument positions are each integer or SSE (one
 * instantiation per combination) and the rest are stack slots. The
 * parameter list the script gave says which register holds which argument.
 *
 * Return values are kept as eight bytes: integer-class in RAX, SSE in XMM0.
 *
 * A hook on one object (FromVirtualObject) is KHook's per-instance virtual
 * hook: the vtable slot is hooked for every object that uses it, and the
 * handlers only run for calls whose `this` is that object.
 *
 * Project: Source2Mod
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/gameconfig.h"
#include "core/module.h"
#include "core/shared.h"
#include "utils/log.h"

#include <source2mod/ISource2ModCommands.h>
#include <source2mod/schema/entity/classes/CBaseEntity.h>

#include <tier0/buffer_string.h>

#include <khook.hpp>
#include <sp_callable.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        // enum Source2ModType (gameconfig.inc)
        enum Type : cell_t
        {
            T_Void = 0,
            T_Bool,
            T_Int8,
            T_Int16,
            T_Int32,
            T_Int64,
            T_Float,
            T_Double,
            T_Entity,
            T_Object,
            T_String,
            T_Vector,
            T_VectorValue,
            T_Pointer,
            T_BufferString,
            T_Command,
            T_IntRef,
            T_BoolRef,
            T_FloatRef,
        };

        bool IsSse(Type t)
        {
            return t == T_Float || t == T_Double;
        }

        PluginId CallerId(IPluginContext* ctx)
        {
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            return plugin ? plugin->Id() : 0;
        }

#if defined(_WIN32)
        constexpr int kSlots = 14;
#else
        constexpr int kSlots = 22;   // 6 integer + 8 SSE + 8 stack
#endif

        // The argument registers of one call, as the callbacks got them.
        struct Regs
        {
            uint64_t s[kSlots];
        };

        struct RawHook : HandleObject
        {
            PluginId owner = 0;
            void* address = nullptr;
            void** vtable = nullptr;
            void* instance = nullptr;       // FromVirtualObject: the handlers run only for this `this`
            int index = -1;
            Type ret = T_Void;
            std::vector<Type> params;
            std::vector<int> slot;          // params[i] lives in Regs::s[slot[i]]
            IPluginFunction* pre = nullptr;
            IPluginFunction* post = nullptr;
            KHook::HookID_t id = KHook::INVALID_HOOK;
            bool removing = false;          // RemoveHook asked, KHook not done yet
            bool zombie = false;            // its handle is gone: delete once removed
            uint64_t (*invoke)(void* fn, const Regs& regs) = nullptr;   // a call in this hook's register shape
        };

        // KHook is done with a hook it was asked to remove.
        void OnRemoved(KHook::HookID_t, void* ctx)
        {
            auto* hook = static_cast<RawHook*>(ctx);
            hook->removing = false;
            if (hook->zombie)
                delete hook;
        }

        void BeginRemove(RawHook* hook)
        {
            if (hook->id == KHook::INVALID_HOOK)
                return;
            const KHook::HookID_t id = hook->id;
            hook->id = KHook::INVALID_HOOK;
            hook->removing = true;
            KHook::RemoveHook(id, true, OnRemoved, hook);
        }

        // What the script's handlers are handed.
        struct Frame
        {
            RawHook* hook;
            Regs regs;
            bool post;
            uint64_t ret;
            bool retSet;
            bool changed;
            // Command parameters lent as Source2ModCommandArgs, freed after the handler.
            std::vector<std::unique_ptr<Source2ModCommandArgs>> commands;
            std::vector<cell_t> commandHandles;
            // Strings SetString put in a parameter: alive until the call (made
            // inside Handle, see the recall there) is over.
            std::vector<std::unique_ptr<std::string>> strings;
        };

        HandleType HookType()
        {
            static const HandleType t = Handles().CreateType("Source2ModHook", [](void* p)
            {
                auto* hook = static_cast<RawHook*>(p);
                hook->pre = hook->post = nullptr;
                // KHook takes the detour down when no call is inside it;
                // the object goes after that.
                BeginRemove(hook);
                if (hook->removing)
                    hook->zombie = true;
                else
                    delete hook;
            });
            return t;
        }

        HandleType ParamsType()
        {
            static const HandleType t = Handles().CreateType("Source2ModHookParams", nullptr, 0, false);
            return t;
        }

        HandleType ReturnType()
        {
            static const HandleType t = Handles().CreateType("Source2ModHookReturn", nullptr, 0, false);
            return t;
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

        // pre / post: the script's handler, then the action for KHook.
        uint64_t Handle(const Regs& regs, bool post)
        {
            auto* hook = static_cast<RawHook*>(KHook::GetContextPtr());
            const bool hasValue = hook->ret != T_Void;
            IPluginFunction* fn = post ? hook->post : hook->pre;

            Frame frame{ hook, regs, post, 0, false, false, {}, {}, {} };
            if (post && hasValue)
                if (void* original = KHook::GetOriginalValuePtr())
                    frame.ret = *static_cast<uint64_t*>(original);

            // A hook on one object: other objects' calls pass untouched.
            const bool mine = !hook->instance || regs.s[0] == reinterpret_cast<uintptr_t>(hook->instance);

            cell_t res = 0;
            if (fn && !hook->zombie && mine)
            {
                const cell_t params = Handles().Create(ParamsType(), &frame, hook->owner);
                const cell_t ret = Handles().Create(ReturnType(), &frame, hook->owner);
                sp::CallArgs args;
                args.PushCell(params);
                args.PushCell(ret);
                args.PushCell(post ? 1 : 0);
                Invoke(fn, args, &res);
                Handles().Free(params, 0, true);
                Handles().Free(ret, 0, true);
                for (cell_t h : frame.commandHandles)
                    Handles().Free(h, 0, true);
            }

            Action action = static_cast<Action>(std::clamp<cell_t>(res, 0, 2));
            uint64_t value = frame.ret;

            // Changed arguments go on the way KHook::Recall sends them: the
            // detour is entered again with the new registers and carries on
            // from the next hook, so every later hook and the original get
            // them -- whichever hook's call-original callback KHook uses.
            if (!post && frame.changed && action != Action::Ignore && hook->invoke)
            {
                void* again = hasValue
                    ? KHook::DoRecall(action, &value, sizeof(value), reinterpret_cast<void*>(KHook::init_operator<uint64_t>),
                                      reinterpret_cast<void*>(KHook::deinit_operator<uint64_t>))
                    : KHook::DoRecall(action, nullptr, 0, nullptr, nullptr);
                hook->invoke(again, frame.regs);
            }

            SaveReturn(action, &value, hasValue, false);
            return value;
        }

        // make-return and call-original are the FIRST hook's on the function,
        // whichever plugin's that is, and KHook's context pointer is not that
        // hook there: neither may read the RawHook. The original's value is
        // always kept as eight bytes (harmless for a void function), and the
        // value handed back is whatever is current.
        uint64_t MakeReturn()
        {
            uint64_t value = 0;
            if (void* current = KHook::GetCurrentValuePtr(true))
                value = *static_cast<uint64_t*>(current);
            KHook::DestroyReturnValue();
            return value;
        }

        // ---- the per-platform callback shapes -----------------------------------

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

        struct CallbackSet
        {
            void* pre;
            void* post;
            void* ret;
            void* original;
            uint64_t (*invoke)(void*, const Regs&);
        };

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

        // Positions: one slot per parameter, in order.
        bool Layout(RawHook* hook, int* mask, std::string& error)
        {
            if (hook->params.size() > static_cast<size_t>(kSlots))
            {
                error = "too many parameters (at most 14)";
                return false;
            }
            *mask = 0;
            hook->slot.clear();
            for (size_t i = 0; i < hook->params.size(); i++)
            {
                if (IsSse(hook->params[i]) && i < 4)
                    *mask |= 1 << i;
                hook->slot.push_back(static_cast<int>(i));
            }
            return true;
        }

        unsigned StackSize(const RawHook* hook)
        {
            size_t args = hook->params.size() + (hook->ret != T_Void ? 1 : 0);
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

        struct CallbackSet
        {
            void* pre;
            void* post;
            void* ret;
            void* original;
            uint64_t (*invoke)(void*, const Regs&);
        };

        template <typename R>
        CallbackSet Pick(int)
        {
            using C = Callbacks<R>;
            return { reinterpret_cast<void*>(&C::Pre), reinterpret_cast<void*>(&C::Post), reinterpret_cast<void*>(&C::Return),
                     reinterpret_cast<void*>(&C::CallOriginal), &C::Invoke };
        }

        // By class: integer parameters take s[0..5] in order, SSE ones s[6..13];
        // what overflows its class takes the next stack slot, s[14..21].
        bool Layout(RawHook* hook, int* mask, std::string& error)
        {
            *mask = 0;
            hook->slot.clear();
            int ni = 0, nf = 0, ns = 0;
            for (Type t : hook->params)
            {
                int slot;
                if (IsSse(t) && nf < 8)
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
        unsigned StackSize(const RawHook* hook)
        {
            return static_cast<unsigned>((hook->ret != T_Void ? 8 : 0) + 8 * hook->params.size());
        }
#endif

        // ---- natives -------------------------------------------------------------------

        bool ValidType(cell_t t)
        {
            return t >= T_Void && t <= T_FloatRef && t != T_VectorValue;
        }

        bool IsRef(Type t)
        {
            return t == T_IntRef || t == T_BoolRef || t == T_FloatRef;
        }

        // An optional string argument at params[n]: "" when the script was
        // compiled against an include without it.
        const char* OptionalString(IPluginContext* ctx, const cell_t* params, int n)
        {
            return params[0] >= n ? LocalString(ctx, params[n]) : "";
        }

        // A signature in a module, optionally only in one section (".text").
        uintptr_t FindSignature(const char* moduleName, const char* signature, const char* section)
        {
            Source2ModModule module(moduleName);
            if (!section || !*section)
                return module.FindPattern(signature).GetAddr();
            if (!module.GetSectionByName(section).IsValid())
                return 0;
            return module.FindPatternInSection(signature, section).GetAddr();
        }

        cell_t NewHook(IPluginContext* ctx, void* address, void** vtable, int index, cell_t returnType, void* instance = nullptr)
        {
            if (!ValidType(returnType) || returnType == T_String || returnType == T_Vector || returnType == T_BufferString || returnType == T_Command ||
                IsRef(static_cast<Type>(returnType)))
                return ctx->ThrowNativeError("Unsupported return type %d", returnType);
            auto* hook = new RawHook();
            hook->owner = CallerId(ctx);
            hook->address = address;
            hook->vtable = vtable;
            hook->instance = instance;
            hook->index = index;
            hook->ret = static_cast<Type>(returnType);
            const cell_t h = Handles().Create(HookType(), hook, hook->owner);
            if (!h)
                delete hook;
            return h;
        }

        cell_t N_HookFromGameConfig(IPluginContext* ctx, const cell_t* params)
        {
            if (!shared::g_pGameConfig)
                return 0;
            const uintptr_t addr = shared::g_pGameConfig->ResolveSignature(LocalString(ctx, params[1])).GetAddr();
            return addr ? NewHook(ctx, reinterpret_cast<void*>(addr), nullptr, -1, params[2]) : 0;
        }

        // FromSignature(module, signature, returnType, const char[] section = "")
        cell_t N_HookFromSignature(IPluginContext* ctx, const cell_t* params)
        {
            const uintptr_t addr = FindSignature(LocalString(ctx, params[1]), LocalString(ctx, params[2]), OptionalString(ctx, params, 4));
            return addr ? NewHook(ctx, reinterpret_cast<void*>(addr), nullptr, -1, params[3]) : 0;
        }

        // FromSignatureRelative(module, signature, returnType, int offset = 1, int nextInstructionOffset = 5, const char[] section = "")
        cell_t N_HookFromSignatureRelative(IPluginContext* ctx, const cell_t* params)
        {
            const uintptr_t match = FindSignature(LocalString(ctx, params[1]), LocalString(ctx, params[2]), OptionalString(ctx, params, 6));
            if (!match)
                return 0;
            const uintptr_t addr = ISource2ModMemory(match).ResolveRelativeAddress(params[4], params[5]).GetAddr();
            return addr ? NewHook(ctx, reinterpret_cast<void*>(addr), nullptr, -1, params[3]) : 0;
        }

        // FromSymbol(module, symbol, returnType)
        cell_t N_HookFromSymbol(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModModule module(LocalString(ctx, params[1]));
            const uintptr_t addr = module.GetFunctionByName(LocalString(ctx, params[2])).GetAddr();
            return addr ? NewHook(ctx, reinterpret_cast<void*>(addr), nullptr, -1, params[3]) : 0;
        }

        // FromVirtual(module, vtableName, index, returnType, const char[] baseName = "")
        cell_t N_HookFromVirtual(IPluginContext* ctx, const cell_t* params)
        {
            if (params[3] < 0)
                return ctx->ThrowNativeError("Invalid vtable index %d", params[3]);
            Source2ModModule module(LocalString(ctx, params[1]));
            const char* name = LocalString(ctx, params[2]);
            const char* base = OptionalString(ctx, params, 5);
            void** vtable = reinterpret_cast<void**>((*base ? module.GetVirtualTableByBase(name, base) : module.GetVirtualTableByName(name)).GetAddr());
            return vtable ? NewHook(ctx, nullptr, vtable, params[3], params[4]) : 0;
        }

        // FromVirtualSecondary(module, vtableName, int offset, int index, returnType):
        // the vtable at this offset-to-top (a secondary base's).
        cell_t N_HookFromVirtualSecondary(IPluginContext* ctx, const cell_t* params)
        {
            if (params[4] < 0)
                return ctx->ThrowNativeError("Invalid vtable index %d", params[4]);
            Source2ModModule module(LocalString(ctx, params[1]));
            void** vtable = reinterpret_cast<void**>(module.GetVirtualTableByOffset(LocalString(ctx, params[2]), params[3]).GetAddr());
            return vtable ? NewHook(ctx, nullptr, vtable, params[4], params[5]) : 0;
        }

        // FromVirtualObject(any instance, int index, returnType): the vtable the
        // object uses, the handlers only for calls on it.
        cell_t N_HookFromVirtualObject(IPluginContext* ctx, const cell_t* params)
        {
            if (params[2] < 0)
                return ctx->ThrowNativeError("Invalid vtable index %d", params[2]);
            void* instance = SchemaObjectPointer(ctx, params[1]);
            if (!instance)
                return 0;
            void** vtable = *static_cast<void***>(instance);
            return vtable ? NewHook(ctx, nullptr, vtable, params[2], params[3], instance) : 0;
        }

        // FromAddress(Source2ModAddress address, Source2ModType returnType)
        cell_t N_HookFromAddress(IPluginContext* ctx, const cell_t* params)
        {
            void* addr = AddressOf(ctx, params[1]);
            return addr ? NewHook(ctx, addr, nullptr, -1, params[2]) : 0;
        }

        RawHook* HookOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<RawHook*>(ReadHandle(ctx, handle, HookType()));
        }

        cell_t N_HookAddParam(IPluginContext* ctx, const cell_t* params)
        {
            RawHook* hook = HookOf(ctx, params[1]);
            if (!hook)
                return 0;
            if (hook->id != KHook::INVALID_HOOK)
                return ctx->ThrowNativeError("The hook is enabled; describe its parameters first");
            if (!ValidType(params[2]) || params[2] == T_Void)
                return ctx->ThrowNativeError("Invalid parameter type %d (a Vector by value cannot be hooked)", params[2]);
            hook->params.push_back(static_cast<Type>(params[2]));
            return 0;
        }

        IPluginFunction* OptionalFunction(IPluginContext* ctx, cell_t id)
        {
            return id == 0 || id == -1 ? nullptr : ctx->GetFunctionById(static_cast<funcid_t>(id));
        }

        // Enable(pre = INVALID_FUNCTION, post = INVALID_FUNCTION)
        cell_t N_HookEnable(IPluginContext* ctx, const cell_t* params)
        {
            RawHook* hook = HookOf(ctx, params[1]);
            if (!hook)
                return 0;
            hook->pre = OptionalFunction(ctx, params[2]);
            hook->post = OptionalFunction(ctx, params[3]);
            if (hook->id != KHook::INVALID_HOOK)
                return 1;
            if (hook->removing)
                return ctx->ThrowNativeError("The hook is still being removed; enable it again next frame");

            int mask = 0;
            std::string error;
            if (!Layout(hook, &mask, error))
                return ctx->ThrowNativeError("Source2ModHook: %s", error.c_str());

            const CallbackSet cb = (hook->ret == T_Float || hook->ret == T_Double) ? Pick<double>(mask) : Pick<uint64_t>(mask);
            hook->invoke = cb.invoke;
            if (hook->vtable)
                hook->id = KHook::SetupVirtualHook(hook->vtable, hook->index, hook, nullptr, cb.pre, cb.post, cb.ret, cb.original, StackSize(hook), true);
            else
                hook->id = KHook::SetupHook(hook->address, hook, nullptr, cb.pre, cb.post, cb.ret, cb.original, StackSize(hook), true);
            return hook->id != KHook::INVALID_HOOK;
        }

        cell_t N_HookDisable(IPluginContext* ctx, const cell_t* params)
        {
            RawHook* hook = HookOf(ctx, params[1]);
            if (!hook || hook->id == KHook::INVALID_HOOK)
                return 0;
            // Handlers off now; the detour comes down when no call is inside it.
            hook->pre = hook->post = nullptr;
            BeginRemove(hook);
            return 0;
        }

        // property bool Enabled: installed (ISource2ModHook::IsInstalled)
        cell_t N_HookGetEnabled(IPluginContext* ctx, const cell_t* params)
        {
            RawHook* hook = HookOf(ctx, params[1]);
            return hook && hook->id != KHook::INVALID_HOOK;
        }

        // ---- Source2ModHookParams / Source2ModHookReturn ------------------------------

        Frame* ParamFrame(IPluginContext* ctx, const cell_t* params, int* slot, Type* type)
        {
            auto* frame = static_cast<Frame*>(ReadHandle(ctx, params[1], ParamsType()));
            if (!frame)
                return nullptr;
            const cell_t num = params[2];
            if (num < 1 || static_cast<size_t>(num) > frame->hook->params.size())
            {
                ctx->ThrowNativeError("Invalid parameter number %d (the hook has %d)", num, static_cast<int>(frame->hook->params.size()));
                return nullptr;
            }
            *slot = frame->hook->slot[num - 1];
            *type = frame->hook->params[num - 1];
            return frame;
        }

        cell_t IntOf(uint64_t bits, Type t)
        {
            switch (t)
            {
            case T_Bool:
                return (bits & 0xFF) != 0;
            case T_Int8:
                return static_cast<int8_t>(bits);
            case T_Int16:
                return static_cast<int16_t>(bits);
            // Out-parameters: the value they point at (0 for null).
            case T_IntRef:
                return bits ? *reinterpret_cast<const int32_t*>(bits) : 0;
            case T_BoolRef:
                return bits ? *reinterpret_cast<const bool*>(bits) : 0;
            case T_FloatRef:
                return bits ? static_cast<cell_t>(*reinterpret_cast<const float*>(bits)) : 0;
            default:
                return static_cast<cell_t>(bits);
            }
        }

        float FloatOf(uint64_t bits, Type t)
        {
            if (t == T_FloatRef)
                return bits ? *reinterpret_cast<const float*>(bits) : 0.0f;
            return t == T_Double ? static_cast<float>(FromBits<double>(bits)) : FromBits<float>(bits);
        }

        // An out-parameter written in place (no Action needed); false for
        // another type. A null pointer is a script error.
        bool SetRef(IPluginContext* ctx, uint64_t bits, Type t, cell_t value, bool isFloat)
        {
            if (!IsRef(t))
                return false;
            if (!bits)
            {
                ctx->ThrowNativeError("The out-parameter is null");
                return true;
            }
            switch (t)
            {
            case T_IntRef:
                *reinterpret_cast<int32_t*>(bits) = isFloat ? static_cast<int32_t>(sp_ctof(value)) : value;
                break;
            case T_BoolRef:
                *reinterpret_cast<bool*>(bits) = value != 0;
                break;
            default:
                *reinterpret_cast<float*>(bits) = isFloat ? sp_ctof(value) : static_cast<float>(value);
                break;
            }
            return true;
        }

        cell_t EntityOf(uint64_t bits)
        {
            auto* e = reinterpret_cast<CEntityInstance*>(bits);
            return e ? static_cast<cell_t>(e->GetEntityIndex().Get()) : -1;
        }

        cell_t N_PGetInt(IPluginContext* ctx, const cell_t* params)
        {
            int slot;
            Type t;
            Frame* f = ParamFrame(ctx, params, &slot, &t);
            return f ? IntOf(f->regs.s[slot], t) : 0;
        }

        cell_t N_PGetFloat(IPluginContext* ctx, const cell_t* params)
        {
            int slot;
            Type t;
            Frame* f = ParamFrame(ctx, params, &slot, &t);
            return f ? sp_ftoc(FloatOf(f->regs.s[slot], t)) : 0;
        }

        cell_t N_PGetEntity(IPluginContext* ctx, const cell_t* params)
        {
            int slot;
            Type t;
            Frame* f = ParamFrame(ctx, params, &slot, &t);
            return f ? EntityOf(f->regs.s[slot]) : -1;
        }

        cell_t N_PGetObject(IPluginContext* ctx, const cell_t* params)
        {
            int slot;
            Type t;
            Frame* f = ParamFrame(ctx, params, &slot, &t);
            return f ? SchemaObjectForPointer(reinterpret_cast<void*>(f->regs.s[slot])) : 0;
        }

        cell_t N_PGetString(IPluginContext* ctx, const cell_t* params)
        {
            int slot;
            Type t;
            Frame* f = ParamFrame(ctx, params, &slot, &t);
            if (!f)
                return 0;
            std::string text;
            const uint64_t bits = f->regs.s[slot];
            switch (t)
            {
            case T_Int64:
                text = std::to_string(static_cast<int64_t>(bits));
                break;
            case T_BufferString:
                if (const auto* b = reinterpret_cast<const CBufferString*>(bits))
                    text = b->Get();
                break;
            case T_Command:
                if (const auto* c = reinterpret_cast<const CCommand*>(bits))
                    text = c->GetCommandString() ? c->GetCommandString() : "";
                break;
            case T_String:
                if (const char* s = reinterpret_cast<const char*>(bits))
                    text = s;
                break;
            default:
                return ctx->ThrowNativeError("Parameter %d is not a string", params[2]);
            }
            size_t written = 0;
            ctx->StringToLocalUTF8(params[3], params[4], text.c_str(), &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_PGetVector(IPluginContext* ctx, const cell_t* params)
        {
            int slot;
            Type t;
            Frame* f = ParamFrame(ctx, params, &slot, &t);
            if (!f)
                return 0;
            const auto* v = reinterpret_cast<const Vector*>(f->regs.s[slot]);
            cell_t* out;
            ctx->LocalToPhysAddr(params[3], &out);
            out[0] = sp_ftoc(v ? v->x : 0.0f);
            out[1] = sp_ftoc(v ? v->y : 0.0f);
            out[2] = sp_ftoc(v ? v->z : 0.0f);
            return 0;
        }

        cell_t N_PSetInt(IPluginContext* ctx, const cell_t* params)
        {
            int slot;
            Type t;
            if (Frame* f = ParamFrame(ctx, params, &slot, &t))
            {
                if (SetRef(ctx, f->regs.s[slot], t, params[3], false))
                    return 0;
                f->regs.s[slot] = static_cast<uint64_t>(static_cast<int64_t>(t == T_Bool ? (params[3] != 0) : params[3]));
                f->changed = true;
            }
            return 0;
        }

        cell_t N_PSetFloat(IPluginContext* ctx, const cell_t* params)
        {
            int slot;
            Type t;
            if (Frame* f = ParamFrame(ctx, params, &slot, &t))
            {
                if (SetRef(ctx, f->regs.s[slot], t, params[3], true))
                    return 0;
                f->regs.s[slot] = t == T_Double ? ToBits<double>(sp_ctof(params[3])) : ToBits<float>(sp_ctof(params[3]));
                f->changed = true;
            }
            return 0;
        }

        cell_t N_PSetEntity(IPluginContext* ctx, const cell_t* params)
        {
            int slot;
            Type t;
            if (Frame* f = ParamFrame(ctx, params, &slot, &t))
            {
                CEntityInstance* e = params[3] == -1 ? nullptr : CBaseEntity::FromIndex<CEntityInstance>(params[3]);
                if (params[3] != -1 && !e)
                    return ctx->ThrowNativeError("Entity %d is invalid", params[3]);
                f->regs.s[slot] = reinterpret_cast<uintptr_t>(e);
                f->changed = true;
            }
            return 0;
        }

        // A Vector parameter is a pointer: the vector is written in place.
        cell_t N_PSetVector(IPluginContext* ctx, const cell_t* params)
        {
            int slot;
            Type t;
            if (Frame* f = ParamFrame(ctx, params, &slot, &t))
            {
                auto* v = reinterpret_cast<Vector*>(f->regs.s[slot]);
                if (!v)
                    return ctx->ThrowNativeError("The vector parameter is null");
                cell_t* in;
                ctx->LocalToPhysAddr(params[3], &in);
                *v = Vector(sp_ctof(in[0]), sp_ctof(in[1]), sp_ctof(in[2]));
            }
            return 0;
        }

        // SetBufferString(int num, const char[] value)
        cell_t N_PSetBufferString(IPluginContext* ctx, const cell_t* params)
        {
            int slot;
            Type t;
            Frame* f = ParamFrame(ctx, params, &slot, &t);
            if (!f)
                return 0;
            if (t != T_BufferString)
                return ctx->ThrowNativeError("Parameter %d is not a BufferString", params[2]);
            auto* b = reinterpret_cast<CBufferString*>(f->regs.s[slot]);
            if (!b)
                return ctx->ThrowNativeError("The BufferString parameter is null");
            b->Clear();
            b->Insert(0, LocalString(ctx, params[3]));
            return 0;
        }

        // GetCommandArgs(int num)
        cell_t N_PGetCommandArgs(IPluginContext* ctx, const cell_t* params)
        {
            int slot;
            Type t;
            Frame* f = ParamFrame(ctx, params, &slot, &t);
            if (!f)
                return 0;
            if (t != T_Command)
                return ctx->ThrowNativeError("Parameter %d is not a Command", params[2]);
            const auto* c = reinterpret_cast<const CCommand*>(f->regs.s[slot]);
            if (!c)
                return 0;
            f->commands.push_back(std::make_unique<Source2ModCommandArgs>(c->ArgC(), c->ArgV(), c->ArgS(), c->GetCommandString()));
            const cell_t h = LendCommandArgs(f->commands.back().get(), f->hook->owner);
            f->commandHandles.push_back(h);
            return h;
        }

        // SetObject(int num, SchemaObject value): an Object / Pointer parameter; 0 is null.
        cell_t N_PSetObject(IPluginContext* ctx, const cell_t* params)
        {
            int slot;
            Type t;
            Frame* f = ParamFrame(ctx, params, &slot, &t);
            if (!f)
                return 0;
            void* ptr = params[3] == 0 ? nullptr : SchemaObjectPointer(ctx, params[3]);
            if (params[3] != 0 && !ptr)
                return 0;
            f->regs.s[slot] = reinterpret_cast<uintptr_t>(ptr);
            f->changed = true;
            return 0;
        }

        // SetString(int num, const char[] value): a String parameter points at a
        // copy that lives until the hooked call is over.
        cell_t N_PSetString(IPluginContext* ctx, const cell_t* params)
        {
            int slot;
            Type t;
            Frame* f = ParamFrame(ctx, params, &slot, &t);
            if (!f)
                return 0;
            if (t != T_String)
                return ctx->ThrowNativeError("Parameter %d is not a String", params[2]);
            f->strings.push_back(std::make_unique<std::string>(LocalString(ctx, params[3])));
            f->regs.s[slot] = reinterpret_cast<uintptr_t>(f->strings.back()->c_str());
            f->changed = true;
            return 0;
        }

        // SetInt64(int num, const char[] value): an Int64 parameter from decimal text.
        cell_t N_PSetInt64(IPluginContext* ctx, const cell_t* params)
        {
            int slot;
            Type t;
            Frame* f = ParamFrame(ctx, params, &slot, &t);
            if (!f)
                return 0;
            if (t != T_Int64)
                return ctx->ThrowNativeError("Parameter %d is not an Int64", params[2]);
            f->regs.s[slot] = static_cast<uint64_t>(strtoll(LocalString(ctx, params[3]), nullptr, 10));
            f->changed = true;
            return 0;
        }

        // A return value (eight bytes) as the cell a script gets for this type.
        cell_t ReturnCell(uint64_t bits, Type t)
        {
            switch (t)
            {
            case T_Float:
            case T_Double:
                return sp_ftoc(FloatOf(bits, t));
            case T_Entity:
                return EntityOf(bits);
            case T_Object:
            case T_Pointer:
                return SchemaObjectForPointer(reinterpret_cast<void*>(bits));
            case T_Void:
                return 0;
            default:
                return IntOf(bits, t);
            }
        }

        // CallOriginal() (pre): the original now, past every hook, with the
        // current parameters. Its value becomes the return value.
        cell_t N_PCallOriginal(IPluginContext* ctx, const cell_t* params)
        {
            auto* f = static_cast<Frame*>(ReadHandle(ctx, params[1], ParamsType()));
            if (!f)
                return 0;
            if (f->post)
                return ctx->ThrowNativeError("CallOriginal is for pre handlers (in post the original has run)");
            RawHook* hook = f->hook;
            void* original = hook->vtable ? KHook::FindOriginal(KHook::FindOriginalVirtual(hook->vtable, hook->index)) : KHook::FindOriginal(hook->address);
            if (!original || !hook->invoke)
                return ctx->ThrowNativeError("The original function is not available");
            const uint64_t value = hook->invoke(original, f->regs);
            if (hook->ret == T_Void)
                return 0;
            f->ret = value;
            f->retSet = true;
            return ReturnCell(value, hook->ret);
        }

        Frame* RetFrame(IPluginContext* ctx, cell_t handle)
        {
            auto* frame = static_cast<Frame*>(ReadHandle(ctx, handle, ReturnType()));
            if (frame && frame->hook->ret == T_Void)
            {
                ctx->ThrowNativeError("The hooked function returns nothing");
                return nullptr;
            }
            return frame;
        }

        cell_t N_RGetInt(IPluginContext* ctx, const cell_t* params)
        {
            Frame* f = RetFrame(ctx, params[1]);
            return f ? IntOf(f->ret, f->hook->ret) : 0;
        }

        cell_t N_RGetFloat(IPluginContext* ctx, const cell_t* params)
        {
            Frame* f = RetFrame(ctx, params[1]);
            return f ? sp_ftoc(FloatOf(f->ret, f->hook->ret)) : 0;
        }

        cell_t N_RGetEntity(IPluginContext* ctx, const cell_t* params)
        {
            Frame* f = RetFrame(ctx, params[1]);
            return f ? EntityOf(f->ret) : -1;
        }

        cell_t N_RSetInt(IPluginContext* ctx, const cell_t* params)
        {
            if (Frame* f = RetFrame(ctx, params[1]))
            {
                f->ret = static_cast<uint64_t>(static_cast<int64_t>(f->hook->ret == T_Bool ? (params[2] != 0) : params[2]));
                f->retSet = true;
            }
            return 0;
        }

        cell_t N_RSetFloat(IPluginContext* ctx, const cell_t* params)
        {
            if (Frame* f = RetFrame(ctx, params[1]))
            {
                f->ret = f->hook->ret == T_Double ? ToBits<double>(sp_ctof(params[2])) : ToBits<float>(sp_ctof(params[2]));
                f->retSet = true;
            }
            return 0;
        }

        cell_t N_RSetEntity(IPluginContext* ctx, const cell_t* params)
        {
            if (Frame* f = RetFrame(ctx, params[1]))
            {
                CEntityInstance* e = params[2] == -1 ? nullptr : CBaseEntity::FromIndex<CEntityInstance>(params[2]);
                f->ret = reinterpret_cast<uintptr_t>(e);
                f->retSet = true;
            }
            return 0;
        }

        cell_t N_RGetObject(IPluginContext* ctx, const cell_t* params)
        {
            Frame* f = RetFrame(ctx, params[1]);
            return f ? SchemaObjectForPointer(reinterpret_cast<void*>(f->ret)) : 0;
        }

        // SetObject(SchemaObject value); 0 is null.
        cell_t N_RSetObject(IPluginContext* ctx, const cell_t* params)
        {
            Frame* f = RetFrame(ctx, params[1]);
            if (!f)
                return 0;
            void* ptr = params[2] == 0 ? nullptr : SchemaObjectPointer(ctx, params[2]);
            if (params[2] != 0 && !ptr)
                return 0;
            f->ret = reinterpret_cast<uintptr_t>(ptr);
            f->retSet = true;
            return 0;
        }

        // GetInt64(char[] buffer, int maxlen): decimal text.
        cell_t N_RGetInt64(IPluginContext* ctx, const cell_t* params)
        {
            Frame* f = RetFrame(ctx, params[1]);
            if (!f)
                return 0;
            const int64_t value = f->hook->ret == T_Int64 ? static_cast<int64_t>(f->ret) : IntOf(f->ret, f->hook->ret);
            size_t written = 0;
            ctx->StringToLocalUTF8(params[2], params[3], std::to_string(value).c_str(), &written);
            return static_cast<cell_t>(written);
        }

        // SetInt64(const char[] value): from decimal text.
        cell_t N_RSetInt64(IPluginContext* ctx, const cell_t* params)
        {
            if (Frame* f = RetFrame(ctx, params[1]))
            {
                f->ret = static_cast<uint64_t>(strtoll(LocalString(ctx, params[2]), nullptr, 10));
                f->retSet = true;
            }
            return 0;
        }

        const sp_nativeinfo_t g_rawHookNatives[] = {
            { "Source2ModHook.FromGameConfig", N_HookFromGameConfig },
            { "Source2ModHook.FromSignature", N_HookFromSignature },
            { "Source2ModHook.FromSignatureRelative", N_HookFromSignatureRelative },
            { "Source2ModHook.FromSymbol", N_HookFromSymbol },
            { "Source2ModHook.FromVirtual", N_HookFromVirtual },
            { "Source2ModHook.FromVirtualSecondary", N_HookFromVirtualSecondary },
            { "Source2ModHook.FromVirtualObject", N_HookFromVirtualObject },
            { "Source2ModHook.FromAddress", N_HookFromAddress },
            { "Source2ModHook.AddParam", N_HookAddParam },
            { "Source2ModHook.Enable", N_HookEnable },
            { "Source2ModHook.Disable", N_HookDisable },
            { "Source2ModHook.Enabled.get", N_HookGetEnabled },
            { "Source2ModHookParams.GetInt64", N_PGetString },
            { "Source2ModHookParams.SetInt64", N_PSetInt64 },
            { "Source2ModHookParams.SetObject", N_PSetObject },
            { "Source2ModHookParams.SetString", N_PSetString },
            { "Source2ModHookParams.CallOriginal", N_PCallOriginal },
            { "Source2ModHookReturn.GetObject", N_RGetObject },
            { "Source2ModHookReturn.SetObject", N_RSetObject },
            { "Source2ModHookReturn.GetInt64", N_RGetInt64 },
            { "Source2ModHookReturn.SetInt64", N_RSetInt64 },
            { "Source2ModHookParams.GetInt", N_PGetInt },
            { "Source2ModHookParams.GetFloat", N_PGetFloat },
            { "Source2ModHookParams.GetBool", N_PGetInt },
            { "Source2ModHookParams.GetEntity", N_PGetEntity },
            { "Source2ModHookParams.GetObject", N_PGetObject },
            { "Source2ModHookParams.GetString", N_PGetString },
            { "Source2ModHookParams.GetVector", N_PGetVector },
            { "Source2ModHookParams.SetInt", N_PSetInt },
            { "Source2ModHookParams.SetFloat", N_PSetFloat },
            { "Source2ModHookParams.SetBool", N_PSetInt },
            { "Source2ModHookParams.SetEntity", N_PSetEntity },
            { "Source2ModHookParams.SetVector", N_PSetVector },
            { "Source2ModHookParams.SetBufferString", N_PSetBufferString },
            { "Source2ModHookParams.GetCommandArgs", N_PGetCommandArgs },
            { "Source2ModHookReturn.GetInt", N_RGetInt },
            { "Source2ModHookReturn.GetFloat", N_RGetFloat },
            { "Source2ModHookReturn.GetBool", N_RGetInt },
            { "Source2ModHookReturn.GetEntity", N_RGetEntity },
            { "Source2ModHookReturn.SetInt", N_RSetInt },
            { "Source2ModHookReturn.SetFloat", N_RSetFloat },
            { "Source2ModHookReturn.SetBool", N_RSetInt },
            { "Source2ModHookReturn.SetEntity", N_RSetEntity },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetRawHookNatives()
    {
        return g_rawHookNatives;
    }
}
