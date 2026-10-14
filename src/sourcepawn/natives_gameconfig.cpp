/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * Gamedata and calling game functions from SourcePawn
 * (source2mod/gameconfig.inc): Source2ModGameConfig on the core's gamedata,
 * and Source2ModFunction -- a function found by gamedata entry, signature
 * or vtable slot, with its parameters described at runtime.
 *
 * The call itself goes through one function pointer type per calling
 * convention that covers every register a call of this shape can use: on
 * SysV x86-64 six integer and eight SSE registers, assigned per class in
 * order; on Windows x64 four positions that are either integer or SSE, then
 * stack slots. A float travels as the low half of an SSE register, which is
 * what the callee reads. More arguments than that are refused.
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

#include <sp_callable.h>

#include <cstring>
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
        };

        PluginId CallerId(IPluginContext* ctx)
        {
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            return plugin ? plugin->Id() : 0;
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

        // ---- Source2ModFunction -----------------------------------------------------

        struct Function : HandleObject
        {
            void* address = nullptr;        // null for a virtual: read off the first argument
            int vtableIndex = -1;
            Type returnType = T_Void;
            std::vector<Type> params;
        };

        HandleType FunctionType()
        {
            static const HandleType t = Handles().CreateType("Source2ModFunction", [](void* p) { delete static_cast<Function*>(p); });
            return t;
        }

        bool ValidType(cell_t t)
        {
            return t >= T_Void && t <= T_Pointer;
        }

        cell_t NewFunction(IPluginContext* ctx, void* address, int vtableIndex, cell_t returnType)
        {
            if (!ValidType(returnType) || returnType == T_String || returnType == T_Vector || returnType == T_VectorValue)
                return ctx->ThrowNativeError("Unsupported return type %d", returnType);
            auto* fn = new Function();
            fn->address = address;
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
            return addr ? NewFunction(ctx, reinterpret_cast<void*>(addr), -1, params[2]) : 0;
        }

        // FromSignature(const char[] module, const char[] signature, Source2ModType returnType)
        cell_t N_FnFromSignature(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModModule module(LocalString(ctx, params[1]));
            const uintptr_t addr = module.FindPattern(LocalString(ctx, params[2])).GetAddr();
            return addr ? NewFunction(ctx, reinterpret_cast<void*>(addr), -1, params[3]) : 0;
        }

        // FromVirtual(int vtableIndex, Source2ModType returnType)
        cell_t N_FnFromVirtual(IPluginContext* ctx, const cell_t* params)
        {
            if (params[1] < 0)
                return ctx->ThrowNativeError("Invalid vtable index %d", params[1]);
            return NewFunction(ctx, nullptr, params[1], params[2]);
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
            if (!ValidType(params[2]) || params[2] == T_Void)
                return ctx->ThrowNativeError("Invalid parameter type %d", params[2]);
            fn->params.push_back(static_cast<Type>(params[2]));
            return 0;
        }

        // One argument in machine form: an integer-class value, or SSE bits.
        struct Arg
        {
            bool sse = false;
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
        // SSE-class ones eight, each class independently.
        template <typename R>
        R Dispatch(void* fn, const std::vector<Arg>& args, const char** error)
        {
            uint64_t i[6]{};
            double f[8]{};
            size_t ni = 0, nf = 0;
            for (const Arg& a : args)
            {
                if (a.sse)
                {
                    if (nf == 8)
                    {
                        *error = "too many floating-point parameters (at most 8)";
                        return R{};
                    }
                    f[nf++] = BitsToDouble(a.bits);
                }
                else
                {
                    if (ni == 6)
                    {
                        *error = "too many integer and pointer parameters (at most 6)";
                        return R{};
                    }
                    i[ni++] = a.bits;
                }
            }
            using Fn = R (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, double, double, double, double, double, double, double, double);
            return reinterpret_cast<Fn>(fn)(i[0], i[1], i[2], i[3], i[4], i[5], f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7]);
        }
#endif

        // Call(any ...): the arguments come by reference, one per parameter.
        cell_t N_FnCall(IPluginContext* ctx, const cell_t* params)
        {
            Function* fn = FnOf(ctx, params[1]);
            if (!fn)
                return 0;
            const size_t given = static_cast<size_t>(params[0] - 1);
            if (given != fn->params.size())
                return ctx->ThrowNativeError("Expected %d arguments, got %d", static_cast<int>(fn->params.size()), static_cast<int>(given));

            std::vector<Arg> args;
            std::vector<std::string> strings;
            std::vector<Vector> vectors(fn->params.size());
            std::vector<std::pair<size_t, cell_t>> copyback;     // vector parameter -> script array
            strings.reserve(fn->params.size());

            for (size_t p = 0; p < fn->params.size(); p++)
            {
                cell_t* addr;
                if (ctx->LocalToPhysAddr(params[2 + p], &addr) != SP_ERROR_NONE)
                    return 0;
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
                    a.bits = static_cast<uint64_t>(strtoll(LocalString(ctx, params[2 + p]), nullptr, 10));
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
                        return 0;
                    break;
                }
                case T_Object:
                case T_Pointer:
                {
                    void* ptr = v == 0 ? nullptr : SchemaObjectPointer(ctx, v);
                    if (v != 0 && !ptr)
                        return 0;
                    a.bits = reinterpret_cast<uintptr_t>(ptr);
                    break;
                }
                case T_String:
                    strings.emplace_back(LocalString(ctx, params[2 + p]));
                    a.bits = reinterpret_cast<uintptr_t>(strings.back().c_str());
                    break;
                case T_Vector:
                    vectors[p] = Vector(sp_ctof(addr[0]), sp_ctof(addr[1]), sp_ctof(addr[2]));
                    a.bits = reinterpret_cast<uintptr_t>(&vectors[p]);
                    copyback.emplace_back(p, params[2 + p]);
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
                if (fn->params.empty() || !args[0].bits || args[0].sse)
                    return ctx->ThrowNativeError("A virtual function needs its object as the first argument");
                void** vtable = *reinterpret_cast<void***>(args[0].bits);
                target = vtable[fn->vtableIndex];
            }
            if (!target)
                return ctx->ThrowNativeError("The function has no address");

            const char* error = nullptr;
            cell_t result = 0;
            if (fn->returnType == T_Float || fn->returnType == T_Double)
            {
                const double d = Dispatch<double>(target, args, &error);
                if (fn->returnType == T_Double)
                    result = sp_ftoc(static_cast<float>(d));
                else
                {
                    float f;
                    memcpy(&f, &d, sizeof(f));
                    result = sp_ftoc(f);
                }
            }
            else
            {
                const uint64_t r = Dispatch<uint64_t>(target, args, &error);
                switch (fn->returnType)
                {
                case T_Bool:
                    result = (r & 0xFF) != 0;
                    break;
                case T_Int8:
                    result = static_cast<int8_t>(r);
                    break;
                case T_Int16:
                    result = static_cast<int16_t>(r);
                    break;
                case T_Int32:
                case T_Int64:
                    result = static_cast<cell_t>(r);
                    break;
                case T_Entity:
                {
                    auto* e = reinterpret_cast<CEntityInstance*>(r);
                    result = e ? static_cast<cell_t>(e->GetEntityIndex().Get()) : -1;
                    break;
                }
                case T_Object:
                case T_Pointer:
                    result = SchemaObjectForPointer(reinterpret_cast<void*>(r));
                    break;
                default:
                    break;
                }
            }
            if (error)
                return ctx->ThrowNativeError("Source2ModFunction.Call: %s", error);

            for (const auto& [p, addrParam] : copyback)
            {
                cell_t* out;
                if (ctx->LocalToPhysAddr(addrParam, &out) == SP_ERROR_NONE)
                {
                    out[0] = sp_ftoc(vectors[p].x);
                    out[1] = sp_ftoc(vectors[p].y);
                    out[2] = sp_ftoc(vectors[p].z);
                }
            }
            return result;
        }

        const sp_nativeinfo_t g_gameConfigNatives[] = {
            { "Source2ModGameConfig.GetLibrary", N_GCGetLibrary },
            { "Source2ModGameConfig.GetSignature", N_GCGetSignature },
            { "Source2ModGameConfig.GetSymbol", N_GCGetSymbol },
            { "Source2ModGameConfig.GetPatch", N_GCGetPatch },
            { "Source2ModGameConfig.GetOffset", N_GCGetOffset },
            { "Source2ModGameConfig.IsSymbol", N_GCIsSymbol },
            { "Source2ModFunction.FromGameConfig", N_FnFromGameConfig },
            { "Source2ModFunction.FromSignature", N_FnFromSignature },
            { "Source2ModFunction.FromVirtual", N_FnFromVirtual },
            { "Source2ModFunction.AddParam", N_FnAddParam },
            { "Source2ModFunction.Call", N_FnCall },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetGameConfigNatives()
    {
        return g_gameConfigNatives;
    }
}
