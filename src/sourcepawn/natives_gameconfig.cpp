/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * Gamedata and calling game functions from SourcePawn
 * (source2mod/gameconfig.inc): Source2ModGameConfig on the core's gamedata,
 * and Source2ModFunction -- a function found by gamedata entry, signature,
 * symbol or vtable slot, with its parameters described at runtime.
 *
 * The call itself goes through one function pointer type per calling
 * convention that covers every register a call of this shape can use: on
 * SysV x86-64 six integer and eight SSE registers, assigned per class in
 * order, then eight stack slots for what overflows either class (in argument
 * order, as the ABI puts them); on Windows x64 four positions that are either
 * integer or SSE, then stack slots. A float travels as the low half of an SSE
 * register (or stack slot), which is what the callee reads. More arguments
 * than that are refused.
 *
 * CallOriginal (and BypassHooks) call past every hook on the function, as
 * C++'s SOURCE2MOD_ORIGINAL / HookChain::Bypass: KHook::FindOriginal on the
 * address, after KHook::FindOriginalVirtual for a vtable slot.
 *
 * Project: Source2Mod
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/gameconfig.h"
#include "core/module.h"
#include "core/shared.h"

#include <source2mod/schema/entity/classes/CBaseEntity.h>

#include <tier0/buffer_string.h>

#include <khook.hpp>
#include <sp_callable.h>

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

        // enum Source2ModPlatform (gameconfig.inc)
        constexpr cell_t kPlatformLinux = 0;
        constexpr cell_t kPlatformWindows = 1;

        PluginId CallerId(IPluginContext* ctx)
        {
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            return plugin ? plugin->Id() : 0;
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

        // ---- Source2ModGameConfig ------------------------------------------------------

        cell_t ToBuffer(IPluginContext* ctx, cell_t addr, cell_t maxlen, const char* value)
        {
            if (!value)
                return 0;
            ctx->StringToLocalUTF8(addr, maxlen, value, nullptr);
            return 1;
        }

        cell_t N_GCGetLibrary(IPluginContext* ctx, const cell_t* params)
        {
            return shared::g_pGameConfig ? ToBuffer(ctx, params[2], params[3], shared::g_pGameConfig->GetLibrary(LocalString(ctx, params[1]))) : 0;
        }

        cell_t N_GCGetSignature(IPluginContext* ctx, const cell_t* params)
        {
            return shared::g_pGameConfig ? ToBuffer(ctx, params[2], params[3], shared::g_pGameConfig->GetSignature(LocalString(ctx, params[1]))) : 0;
        }

        cell_t N_GCGetSymbol(IPluginContext* ctx, const cell_t* params)
        {
            return shared::g_pGameConfig ? ToBuffer(ctx, params[2], params[3], shared::g_pGameConfig->GetSymbol(LocalString(ctx, params[1]))) : 0;
        }

        cell_t N_GCGetPatch(IPluginContext* ctx, const cell_t* params)
        {
            return shared::g_pGameConfig ? ToBuffer(ctx, params[2], params[3], shared::g_pGameConfig->GetPatch(LocalString(ctx, params[1]))) : 0;
        }

        cell_t N_GCGetOffset(IPluginContext* ctx, const cell_t* params)
        {
            return shared::g_pGameConfig ? shared::g_pGameConfig->GetOffset(LocalString(ctx, params[1])) : -1;
        }

        cell_t N_GCIsSymbol(IPluginContext* ctx, const cell_t* params)
        {
            return shared::g_pGameConfig && shared::g_pGameConfig->IsSymbol(LocalString(ctx, params[1]));
        }

        cell_t N_GCGetPlatform(IPluginContext*, const cell_t*)
        {
#if defined(_WIN32)
            return kPlatformWindows;
#else
            return kPlatformLinux;
#endif
        }

        // Resolves(const char[] name): the entry's signature or symbol is found.
        cell_t N_GCResolves(IPluginContext* ctx, const cell_t* params)
        {
            return shared::g_pGameConfig && shared::g_pGameConfig->ResolveSignature(LocalString(ctx, params[1])).GetAddr() != 0;
        }

        // SignatureMatches(const char[] module, const char[] signature, const char[] section = "")
        cell_t N_GCSignatureMatches(IPluginContext* ctx, const cell_t* params)
        {
            return FindSignature(LocalString(ctx, params[1]), LocalString(ctx, params[2]), OptionalString(ctx, params, 3)) != 0;
        }

        // ---- Source2ModFunction -----------------------------------------------------

        struct Function : HandleObject
        {
            void* address = nullptr;        // null for a virtual: read off the vtable
            void** vtable = nullptr;        // FromVirtualTable; null for FromVirtual (the first argument's)
            int vtableIndex = -1;
            Type returnType = T_Void;
            std::vector<Type> params;
            bool bypass = false;            // BypassHooks: every Call* goes past the hooks
        };

        HandleType FunctionType()
        {
            static const HandleType t = Handles().CreateType("Source2ModFunction", [](void* p) { delete static_cast<Function*>(p); });
            return t;
        }

        bool IsRef(cell_t t)
        {
            return t == T_IntRef || t == T_BoolRef || t == T_FloatRef;
        }

        // A parameter type a call can pass.
        bool ValidParam(cell_t t)
        {
            return t > T_Void && t <= T_FloatRef && t != T_Command;
        }

        cell_t NewFunction(IPluginContext* ctx, void* address, void** vtable, int vtableIndex, cell_t returnType)
        {
            if (returnType < T_Void || returnType > T_Pointer)
                return ctx->ThrowNativeError("Unsupported return type %d", returnType);
            auto* fn = new Function();
            fn->address = address;
            fn->vtable = vtable;
            fn->vtableIndex = vtableIndex;
            fn->returnType = static_cast<Type>(returnType);
            const cell_t h = Handles().Create(FunctionType(), fn, CallerId(ctx));
            if (!h)
                delete fn;
            return h;
        }

        // FromGameConfig(const char[] name, Source2ModType returnType)
        cell_t N_FnFromGameConfig(IPluginContext* ctx, const cell_t* params)
        {
            if (!shared::g_pGameConfig)
                return 0;
            const uintptr_t addr = shared::g_pGameConfig->ResolveSignature(LocalString(ctx, params[1])).GetAddr();
            return addr ? NewFunction(ctx, reinterpret_cast<void*>(addr), nullptr, -1, params[2]) : 0;
        }

        // FromSignature(const char[] module, const char[] signature, Source2ModType returnType, const char[] section = "")
        cell_t N_FnFromSignature(IPluginContext* ctx, const cell_t* params)
        {
            const uintptr_t addr = FindSignature(LocalString(ctx, params[1]), LocalString(ctx, params[2]), OptionalString(ctx, params, 4));
            return addr ? NewFunction(ctx, reinterpret_cast<void*>(addr), nullptr, -1, params[3]) : 0;
        }

        // FromSignatureRelative(module, signature, returnType, int offset = 1, int nextInstructionOffset = 5, const char[] section = "")
        // The signature matches a call/jmp/lea; the function is its rel32 target
        // (ISource2ModMemory::ResolveRelativeAddress / FollowNearCall).
        cell_t N_FnFromSignatureRelative(IPluginContext* ctx, const cell_t* params)
        {
            const uintptr_t match = FindSignature(LocalString(ctx, params[1]), LocalString(ctx, params[2]), OptionalString(ctx, params, 6));
            if (!match)
                return 0;
            const uintptr_t addr = ISource2ModMemory(match).ResolveRelativeAddress(params[4], params[5]).GetAddr();
            return addr ? NewFunction(ctx, reinterpret_cast<void*>(addr), nullptr, -1, params[3]) : 0;
        }

        // FromSymbol(const char[] module, const char[] symbol, Source2ModType returnType)
        cell_t N_FnFromSymbol(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModModule module(LocalString(ctx, params[1]));
            const uintptr_t addr = module.GetFunctionByName(LocalString(ctx, params[2])).GetAddr();
            return addr ? NewFunction(ctx, reinterpret_cast<void*>(addr), nullptr, -1, params[3]) : 0;
        }

        // FromVirtual(int vtableIndex, Source2ModType returnType)
        cell_t N_FnFromVirtual(IPluginContext* ctx, const cell_t* params)
        {
            if (params[1] < 0)
                return ctx->ThrowNativeError("Invalid vtable index %d", params[1]);
            return NewFunction(ctx, nullptr, nullptr, params[1], params[2]);
        }

        // FromVirtualTable(const char[] module, const char[] vtableName, int vtableIndex, Source2ModType returnType, const char[] baseName = "")
        cell_t N_FnFromVirtualTable(IPluginContext* ctx, const cell_t* params)
        {
            if (params[3] < 0)
                return ctx->ThrowNativeError("Invalid vtable index %d", params[3]);
            Source2ModModule module(LocalString(ctx, params[1]));
            const char* base = OptionalString(ctx, params, 5);
            const char* name = LocalString(ctx, params[2]);
            auto** vtable = reinterpret_cast<void**>((*base ? module.GetVirtualTableByBase(name, base) : module.GetVirtualTableByName(name)).GetAddr());
            return vtable ? NewFunction(ctx, nullptr, vtable, params[3], params[4]) : 0;
        }

        // FromAddress(Source2ModAddress address, Source2ModType returnType)
        cell_t N_FnFromAddress(IPluginContext* ctx, const cell_t* params)
        {
            void* addr = AddressOf(ctx, params[1]);
            return addr ? NewFunction(ctx, addr, nullptr, -1, params[2]) : 0;
        }

        Function* FnOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<Function*>(ReadHandle(ctx, handle, FunctionType()));
        }

        cell_t N_FnAddParam(IPluginContext* ctx, const cell_t* params)
        {
            Function* fn = FnOf(ctx, params[1]);
            if (!fn)
                return 0;
            if (!ValidParam(params[2]))
                return ctx->ThrowNativeError("Invalid parameter type %d", params[2]);
            fn->params.push_back(static_cast<Type>(params[2]));
            return 0;
        }

        cell_t N_FnGetBypassHooks(IPluginContext* ctx, const cell_t* params)
        {
            Function* fn = FnOf(ctx, params[1]);
            return fn && fn->bypass;
        }

        cell_t N_FnSetBypassHooks(IPluginContext* ctx, const cell_t* params)
        {
            if (Function* fn = FnOf(ctx, params[1]))
                fn->bypass = params[2] != 0;
            return 0;
        }

        // One argument in machine form: an integer-class value, or SSE bits.
        struct Arg
        {
            bool sse = false;
            bool pairHead = false;      // a Vector by value (SysV): this and the next go together
            uint64_t bits = 0;
        };

        uint64_t FloatBits(float f)
        {
            uint32_t u;
            memcpy(&u, &f, sizeof(u));
            return u;
        }

        uint64_t DoubleBits(double d)
        {
            uint64_t u;
            memcpy(&u, &d, sizeof(u));
            return u;
        }

        double BitsToDouble(uint64_t u)
        {
            double d;
            memcpy(&d, &u, sizeof(d));
            return d;
        }

        float BitsToFloat(uint64_t u)
        {
            float f;
            const uint32_t low = static_cast<uint32_t>(u);
            memcpy(&f, &low, sizeof(f));
            return f;
        }

        void* EntityPointer(IPluginContext* ctx, cell_t index, bool* ok)
        {
            *ok = true;
            if (index == -1)
                return nullptr;
            CEntityInstance* e = (index >= 0 && index < 32768) ? CBaseEntity::FromIndex<CEntityInstance>(index) : nullptr;
            if (!e)
            {
                ctx->ThrowNativeError("Entity %d is invalid", index);
                *ok = false;
            }
            return e;
        }

        // XMM0 and XMM1 as a SysV function returning a two-eightbyte SSE struct
        // (a Vector by value) leaves them.
        struct SsePair
        {
            double lo;
            double hi;
        };

#if defined(_WIN32)
        // Windows x64: four positions, each integer or SSE, then stack slots.
        constexpr size_t kMaxArgs = 14;

        template <bool F>
        using Pos = std::conditional_t<F, double, uint64_t>;

        template <bool F>
        Pos<F> At(const Arg* a)
        {
            if constexpr (F)
                return BitsToDouble(a->bits);
            else
                return a->bits;
        }

        template <typename R, bool F0, bool F1, bool F2, bool F3>
        R CallWin(void* fn, const Arg* a)
        {
            using Fn = R (*)(Pos<F0>, Pos<F1>, Pos<F2>, Pos<F3>, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
            return reinterpret_cast<Fn>(fn)(At<F0>(&a[0]), At<F1>(&a[1]), At<F2>(&a[2]), At<F3>(&a[3]),
                                            a[4].bits, a[5].bits, a[6].bits, a[7].bits, a[8].bits, a[9].bits, a[10].bits, a[11].bits, a[12].bits, a[13].bits);
        }

        template <typename R>
        R Dispatch(void* fn, const std::vector<Arg>& args, const char** error)
        {
            if (args.size() > kMaxArgs)
            {
                *error = "too many parameters (at most 14)";
                return R{};
            }
            Arg a[kMaxArgs]{};
            for (size_t i = 0; i < args.size(); i++)
                a[i] = args[i];
            const int mask = (a[0].sse ? 1 : 0) | (a[1].sse ? 2 : 0) | (a[2].sse ? 4 : 0) | (a[3].sse ? 8 : 0);
            switch (mask)
            {
#define S2M_CASE(m) case m: return CallWin<R, (m & 1) != 0, (m & 2) != 0, (m & 4) != 0, (m & 8) != 0>(fn, a);
                S2M_CASE(0) S2M_CASE(1) S2M_CASE(2) S2M_CASE(3) S2M_CASE(4) S2M_CASE(5) S2M_CASE(6) S2M_CASE(7)
                S2M_CASE(8) S2M_CASE(9) S2M_CASE(10) S2M_CASE(11) S2M_CASE(12) S2M_CASE(13) S2M_CASE(14) S2M_CASE(15)
#undef S2M_CASE
            }
            return R{};
        }
#else
        // SysV x86-64: integer-class arguments take six registers in order,
        // SSE-class ones eight, each class independently; what does not fit
        // goes on the stack in argument order, eight bytes each. A Vector by
        // value takes two SSE registers or, when two are not left, two stack
        // slots (the whole struct goes to memory).
        constexpr size_t kStackSlots = 8;

        template <typename R>
        R Dispatch(void* fn, const std::vector<Arg>& args, const char** error)
        {
            uint64_t i[6]{};
            double f[8]{};
            uint64_t s[kStackSlots]{};
            size_t ni = 0, nf = 0, ns = 0;
            auto toStack = [&](uint64_t bits)
            {
                if (ns == kStackSlots)
                    return false;
                s[ns++] = bits;
                return true;
            };
            for (size_t k = 0; k < args.size(); k++)
            {
                const Arg& a = args[k];
                bool ok = true;
                if (a.pairHead && k + 1 < args.size())
                {
                    const Arg& b = args[++k];
                    if (nf + 2 <= 8)
                    {
                        f[nf++] = BitsToDouble(a.bits);
                        f[nf++] = BitsToDouble(b.bits);
                    }
                    else
                        ok = toStack(a.bits) && toStack(b.bits);
                }
                else if (a.sse)
                {
                    if (nf < 8)
                        f[nf++] = BitsToDouble(a.bits);
                    else
                        ok = toStack(a.bits);
                }
                else
                {
                    if (ni < 6)
                        i[ni++] = a.bits;
                    else
                        ok = toStack(a.bits);
                }
                if (!ok)
                {
                    *error = "too many parameters (6 integer and 8 floating-point in registers, then 8 on the stack)";
                    return R{};
                }
            }
            using Fn = R (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, double, double, double, double, double, double, double, double,
                             uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
            return reinterpret_cast<Fn>(fn)(i[0], i[1], i[2], i[3], i[4], i[5], f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7],
                                            s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]);
        }
#endif

        // What a call returned, in every form a Call* native may want.
        struct Result
        {
            uint64_t bits = 0;      // integer class (RAX)
            double sse = 0;         // XMM0
            Vector vec;             // Vector / VectorValue returns
        };

        // A call of `fn` with the script's arguments from params[first] on.
        // false: a script error was thrown.
        bool DoCall(IPluginContext* ctx, const cell_t* params, int first, Function* fn, bool bypass, Result* out)
        {
            const size_t given = params[0] >= first - 1 ? static_cast<size_t>(params[0] - (first - 1)) : 0;
            if (given != fn->params.size())
            {
                ctx->ThrowNativeError("Expected %d arguments, got %d", static_cast<int>(fn->params.size()), static_cast<int>(given));
                return false;
            }

            const size_t n = fn->params.size();
            std::vector<Arg> args;
            std::vector<std::string> strings;
            std::vector<Vector> vectors(n);
            std::vector<int32_t> ints(n);
            std::unique_ptr<bool[]> bools(new bool[n ? n : 1]());
            std::vector<float> floats(n);
            std::vector<std::unique_ptr<CBufferString>> buffers;
            std::vector<std::pair<size_t, cell_t>> copyback;     // out parameter -> script variable
            strings.reserve(n);

            for (size_t p = 0; p < n; p++)
            {
                cell_t* addr;
                if (ctx->LocalToPhysAddr(params[first + p], &addr) != SP_ERROR_NONE)
                    return false;
                const cell_t v = *addr;
                Arg a;
                switch (fn->params[p])
                {
                case T_Bool:
                    a.bits = v ? 1 : 0;
                    break;
                case T_Int8:
                case T_Int16:
                case T_Int32:
                    a.bits = static_cast<uint64_t>(static_cast<int64_t>(v));
                    break;
                case T_Int64:
                    a.bits = static_cast<uint64_t>(strtoll(LocalString(ctx, params[first + p]), nullptr, 10));
                    break;
                case T_Float:
                    a.sse = true;
                    a.bits = FloatBits(sp_ctof(v));
                    break;
                case T_Double:
                    a.sse = true;
                    a.bits = DoubleBits(sp_ctof(v));
                    break;
                case T_Entity:
                {
                    bool ok;
                    a.bits = reinterpret_cast<uintptr_t>(EntityPointer(ctx, v, &ok));
                    if (!ok)
                        return false;
                    break;
                }
                case T_Object:
                case T_Pointer:
                {
                    void* ptr = v == 0 ? nullptr : SchemaObjectPointer(ctx, v);
                    if (v != 0 && !ptr)
                        return false;
                    a.bits = reinterpret_cast<uintptr_t>(ptr);
                    break;
                }
                case T_String:
                    strings.emplace_back(LocalString(ctx, params[first + p]));
                    a.bits = reinterpret_cast<uintptr_t>(strings.back().c_str());
                    break;
                case T_BufferString:
                    buffers.push_back(std::make_unique<CBufferString>(LocalString(ctx, params[first + p])));
                    a.bits = reinterpret_cast<uintptr_t>(buffers.back().get());
                    break;
                case T_Vector:
                    vectors[p] = Vector(sp_ctof(addr[0]), sp_ctof(addr[1]), sp_ctof(addr[2]));
                    a.bits = reinterpret_cast<uintptr_t>(&vectors[p]);
                    copyback.emplace_back(p, params[first + p]);
                    break;
                case T_IntRef:
                    ints[p] = v;
                    a.bits = reinterpret_cast<uintptr_t>(&ints[p]);
                    copyback.emplace_back(p, params[first + p]);
                    break;
                case T_BoolRef:
                    bools[p] = v != 0;
                    a.bits = reinterpret_cast<uintptr_t>(&bools[p]);
                    copyback.emplace_back(p, params[first + p]);
                    break;
                case T_FloatRef:
                    floats[p] = sp_ctof(v);
                    a.bits = reinterpret_cast<uintptr_t>(&floats[p]);
                    copyback.emplace_back(p, params[first + p]);
                    break;
                case T_VectorValue:
                {
                    vectors[p] = Vector(sp_ctof(addr[0]), sp_ctof(addr[1]), sp_ctof(addr[2]));
#if defined(_WIN32)
                    // A 12-byte struct goes by hidden reference on Windows x64.
                    a.bits = reinterpret_cast<uintptr_t>(&vectors[p]);
#else
                    // Two SSE eightbytes on SysV: x and y packed, then z.
                    Arg xy;
                    xy.sse = true;
                    xy.pairHead = true;
                    xy.bits = FloatBits(vectors[p].x) | (FloatBits(vectors[p].y) << 32);
                    args.push_back(xy);
                    a.sse = true;
                    a.bits = FloatBits(vectors[p].z);
#endif
                    break;
                }
                default:
                    break;
                }
                args.push_back(a);
            }

            void* target = fn->address;
            if (fn->vtableIndex >= 0)
            {
                void** vtable = fn->vtable;
                if (!vtable)
                {
                    if (fn->params.empty() || !args[0].bits || args[0].sse)
                    {
                        ctx->ThrowNativeError("A virtual function needs its object as the first argument");
                        return false;
                    }
                    vtable = *reinterpret_cast<void***>(args[0].bits);
                }
                target = bypass ? KHook::FindOriginal(KHook::FindOriginalVirtual(vtable, fn->vtableIndex)) : vtable[fn->vtableIndex];
            }
            else if (bypass && target)
                target = KHook::FindOriginal(target);
            if (!target)
            {
                ctx->ThrowNativeError("The function has no address");
                return false;
            }

            const char* error = nullptr;
            if (fn->returnType == T_Float || fn->returnType == T_Double)
                out->sse = Dispatch<double>(target, args, &error);
            else if (fn->returnType == T_VectorValue)
            {
#if defined(_WIN32)
                // Returned through a hidden pointer: the first argument of a
                // free function, the second (after `this`) of a member one.
                Vector result;
                const bool member = fn->vtableIndex >= 0 || (!fn->params.empty() && (fn->params[0] == T_Entity || fn->params[0] == T_Object || fn->params[0] == T_Pointer));
                Arg hidden;
                hidden.bits = reinterpret_cast<uintptr_t>(&result);
                args.insert(args.begin() + (member ? 1 : 0), hidden);
                Dispatch<uint64_t>(target, args, &error);
                out->vec = result;
#else
                const SsePair r = Dispatch<SsePair>(target, args, &error);
                const uint64_t xy = DoubleBits(r.lo);
                out->vec = Vector(BitsToFloat(xy), BitsToFloat(xy >> 32), BitsToFloat(DoubleBits(r.hi)));
#endif
            }
            else
            {
                out->bits = Dispatch<uint64_t>(target, args, &error);
                if (fn->returnType == T_Vector && !error)
                    if (const auto* v = reinterpret_cast<const Vector*>(out->bits))
                        out->vec = *v;
            }
            if (error)
            {
                ctx->ThrowNativeError("Source2ModFunction.Call: %s", error);
                return false;
            }

            for (const auto& [p, addrParam] : copyback)
            {
                cell_t* dst;
                if (ctx->LocalToPhysAddr(addrParam, &dst) != SP_ERROR_NONE)
                    continue;
                switch (fn->params[p])
                {
                case T_Vector:
                    dst[0] = sp_ftoc(vectors[p].x);
                    dst[1] = sp_ftoc(vectors[p].y);
                    dst[2] = sp_ftoc(vectors[p].z);
                    break;
                case T_IntRef:
                    dst[0] = ints[p];
                    break;
                case T_BoolRef:
                    dst[0] = bools[p] ? 1 : 0;
                    break;
                case T_FloatRef:
                    dst[0] = sp_ftoc(floats[p]);
                    break;
                default:
                    break;
                }
            }
            return true;
        }

        // The cell Call hands back for a scalar return type.
        cell_t ScalarResult(Type t, const Result& r)
        {
            switch (t)
            {
            case T_Float:
                return sp_ftoc(BitsToFloat(DoubleBits(r.sse)));
            case T_Double:
                return sp_ftoc(static_cast<float>(r.sse));
            case T_Bool:
                return (r.bits & 0xFF) != 0;
            case T_Int8:
                return static_cast<int8_t>(r.bits);
            case T_Int16:
                return static_cast<int16_t>(r.bits);
            case T_Int32:
            case T_Int64:
                return static_cast<cell_t>(r.bits);
            case T_Entity:
            {
                auto* e = reinterpret_cast<CEntityInstance*>(r.bits);
                return e ? static_cast<cell_t>(e->GetEntityIndex().Get()) : -1;
            }
            case T_Object:
            case T_Pointer:
                return SchemaObjectForPointer(reinterpret_cast<void*>(r.bits));
            default:
                return 0;
            }
        }

        cell_t CallScalar(IPluginContext* ctx, const cell_t* params, bool bypass)
        {
            Function* fn = FnOf(ctx, params[1]);
            if (!fn)
                return 0;
            if (fn->returnType == T_String)
                return ctx->ThrowNativeError("The function returns a string: use CallString");
            if (fn->returnType == T_Vector || fn->returnType == T_VectorValue)
                return ctx->ThrowNativeError("The function returns a vector: use CallVector");
            Result r;
            if (!DoCall(ctx, params, 2, fn, bypass || fn->bypass, &r))
                return 0;
            return ScalarResult(fn->returnType, r);
        }

        // Call(any ...): the arguments come by reference, one per parameter.
        cell_t N_FnCall(IPluginContext* ctx, const cell_t* params)
        {
            return CallScalar(ctx, params, false);
        }

        // CallOriginal(any ...): the same past every hook on the function.
        cell_t N_FnCallOriginal(IPluginContext* ctx, const cell_t* params)
        {
            return CallScalar(ctx, params, true);
        }

        // CallInt64(char[] buffer, int maxlen, any ...): the integer return value as decimal text.
        cell_t N_FnCallInt64(IPluginContext* ctx, const cell_t* params)
        {
            Function* fn = FnOf(ctx, params[1]);
            if (!fn)
                return 0;
            if (fn->returnType < T_Bool || fn->returnType > T_Int64)
                return ctx->ThrowNativeError("The function does not return an integer");
            Result r;
            if (!DoCall(ctx, params, 4, fn, fn->bypass, &r))
                return 0;
            const int64_t value = fn->returnType == T_Int64 ? static_cast<int64_t>(r.bits) : ScalarResult(fn->returnType, r);
            size_t written = 0;
            ctx->StringToLocalUTF8(params[2], params[3], std::to_string(value).c_str(), &written);
            return static_cast<cell_t>(written);
        }

        // CallString(char[] buffer, int maxlen, any ...): a const char* return value; "" for null.
        cell_t N_FnCallString(IPluginContext* ctx, const cell_t* params)
        {
            Function* fn = FnOf(ctx, params[1]);
            if (!fn)
                return 0;
            if (fn->returnType != T_String)
                return ctx->ThrowNativeError("The function does not return a String");
            Result r;
            if (!DoCall(ctx, params, 4, fn, fn->bypass, &r))
                return 0;
            const char* s = reinterpret_cast<const char*>(r.bits);
            size_t written = 0;
            ctx->StringToLocalUTF8(params[2], params[3], s ? s : "", &written);
            return static_cast<cell_t>(written);
        }

        // CallVector(float vec[3], any ...): a Vector / Vector* return value; 0,0,0 for null.
        cell_t N_FnCallVector(IPluginContext* ctx, const cell_t* params)
        {
            Function* fn = FnOf(ctx, params[1]);
            if (!fn)
                return 0;
            if (fn->returnType != T_Vector && fn->returnType != T_VectorValue)
                return ctx->ThrowNativeError("The function does not return a Vector");
            Result r;
            if (!DoCall(ctx, params, 3, fn, fn->bypass, &r))
                return 0;
            cell_t* out;
            if (ctx->LocalToPhysAddr(params[2], &out) == SP_ERROR_NONE)
            {
                out[0] = sp_ftoc(r.vec.x);
                out[1] = sp_ftoc(r.vec.y);
                out[2] = sp_ftoc(r.vec.z);
            }
            return 0;
        }

        const sp_nativeinfo_t g_gameConfigNatives[] = {
            { "Source2ModGameConfig.GetLibrary", N_GCGetLibrary },
            { "Source2ModGameConfig.GetSignature", N_GCGetSignature },
            { "Source2ModGameConfig.GetSymbol", N_GCGetSymbol },
            { "Source2ModGameConfig.GetPatch", N_GCGetPatch },
            { "Source2ModGameConfig.GetOffset", N_GCGetOffset },
            { "Source2ModGameConfig.IsSymbol", N_GCIsSymbol },
            { "Source2ModGameConfig.GetPlatform", N_GCGetPlatform },
            { "Source2ModGameConfig.Resolves", N_GCResolves },
            { "Source2ModGameConfig.SignatureMatches", N_GCSignatureMatches },
            { "Source2ModFunction.FromGameConfig", N_FnFromGameConfig },
            { "Source2ModFunction.FromSignature", N_FnFromSignature },
            { "Source2ModFunction.FromSignatureRelative", N_FnFromSignatureRelative },
            { "Source2ModFunction.FromSymbol", N_FnFromSymbol },
            { "Source2ModFunction.FromVirtual", N_FnFromVirtual },
            { "Source2ModFunction.FromVirtualTable", N_FnFromVirtualTable },
            { "Source2ModFunction.FromAddress", N_FnFromAddress },
            { "Source2ModFunction.AddParam", N_FnAddParam },
            { "Source2ModFunction.BypassHooks.get", N_FnGetBypassHooks },
            { "Source2ModFunction.BypassHooks.set", N_FnSetBypassHooks },
            { "Source2ModFunction.Call", N_FnCall },
            { "Source2ModFunction.CallOriginal", N_FnCallOriginal },
            { "Source2ModFunction.CallInt64", N_FnCallInt64 },
            { "Source2ModFunction.CallString", N_FnCallString },
            { "Source2ModFunction.CallVector", N_FnCallVector },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetGameConfigNatives()
    {
        return g_gameConfigNatives;
    }
}
