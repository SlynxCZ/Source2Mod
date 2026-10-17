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

/*
 * HookPoint::Function from SourcePawn (source2mod/hooks.inc):
 * Source2ModHooks.HookFunction / HookFunctionInstance / HookFunctionClass on
 * a Source2ModFunction, and FunctionContext -- the arguments and the return
 * value of one call, by index (`this` of a member function is 0), the same
 * as C++'s FunctionContext. The hooks themselves are the core's
 * (core/dynamichooks.cpp); a script's handler is one of them.
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/dynamichooks.h"
#include "core/hooks.h"

#include <source2mod/ISource2ModCommands.h>
#include <source2mod/schema/entity/classes/CBaseEntity.h>

#include <tier0/buffer_string.h>

#include <sp_callable.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        PluginId CallerId(IPluginContext* ctx)
        {
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            return plugin ? plugin->Id() : 0;
        }

        // What a FunctionContext handle points at for one handler call.
        struct Frame
        {
            dynamichooks::CallFrame* call;
            PluginId owner;
            std::vector<std::unique_ptr<Source2ModCommandArgs>> commands;
            std::vector<cell_t> commandHandles;
        };

        HandleType ContextType()
        {
            static const HandleType t = Handles().CreateType("FunctionContext", nullptr, 0, false);
            return t;
        }

        // The hooks a script made, for Unhook's owner check and its unload.
        std::map<Source2ModHookId, PluginId> g_hooks;

        Source2ModHookId AddHook(IPluginContext* ctx, cell_t fnHandle, cell_t fnId, bool post, const HookTarget& target)
        {
            ISource2ModFunction* function = DescribedFunction(ctx, fnHandle);
            if (!function)
                return 0;
            IPluginFunction* fn = ctx->GetFunctionById(static_cast<funcid_t>(fnId));
            if (!fn)
            {
                ctx->ThrowNativeError("Invalid function id (%X)", fnId);
                return 0;
            }
            const PluginId owner = CallerId(ctx);
            HookHandler<FunctionContext> handler([fn, owner](FunctionContext& c, bool p) -> HookReturn<void>
            {
                Frame frame{ static_cast<dynamichooks::CallFrame*>(&c), owner, {}, {} };
                const cell_t handle = Handles().Create(ContextType(), &frame, owner);
                sp::CallArgs args;
                args.PushCell(handle);
                args.PushCell(p ? 1 : 0);
                cell_t res = 0;
                Invoke(fn, args, &res);
                Handles().Free(handle, 0, true);
                for (cell_t h : frame.commandHandles)
                    Handles().Free(h, 0, true);
                return static_cast<Action>(std::clamp<cell_t>(res, 0, 2));
            });
            const Source2ModHookId id = hooks::hooksManager.AddHookFor(owner, HookPointId::Function, function, target, {}, &handler, post);
            if (!id)
            {
                ctx->ThrowNativeError("The function cannot be hooked (see the console)");
                return 0;
            }
            g_hooks[id] = owner;
            return id;
        }

        void* InstanceArg(IPluginContext* ctx, cell_t value)
        {
            if (IsEntityCell(value))
                return EntityArg(ctx, value);
            return value ? SchemaObjectPointer(ctx, value) : nullptr;
        }

        // HookFunction(Source2ModFunction fn, handler, bool post = false)
        cell_t N_HookFunction(IPluginContext* ctx, const cell_t* params)
        {
            return AddHook(ctx, params[1], params[2], params[3] != 0, HookTarget::All());
        }

        // HookFunctionInstance(Source2ModFunction fn, any instance, handler, bool post = false)
        cell_t N_HookFunctionInstance(IPluginContext* ctx, const cell_t* params)
        {
            void* instance = InstanceArg(ctx, params[2]);
            if (!instance)
                return ctx->ThrowNativeError("Null instance");
            return AddHook(ctx, params[1], params[3], params[4] != 0, HookTarget::Instance(instance));
        }

        // HookFunctionClass(Source2ModFunction fn, const char[] className, handler, bool post = false)
        cell_t N_HookFunctionClass(IPluginContext* ctx, const cell_t* params)
        {
            return AddHook(ctx, params[1], params[3], params[4] != 0, HookTarget::Class(LocalString(ctx, params[2])));
        }

        // ---- FunctionContext -------------------------------------------------------

        Frame* FrameOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<Frame*>(ReadHandle(ctx, handle, ContextType()));
        }

        // The frame and parameter params[2], checked.
        Frame* Param(IPluginContext* ctx, const cell_t* params, Source2ModType* type)
        {
            Frame* f = FrameOf(ctx, params[1]);
            if (!f)
                return nullptr;
            const cell_t index = params[2];
            if (index < 0 || index >= f->call->Count())
            {
                ctx->ThrowNativeError("Invalid parameter index %d (the function has %d, `this` is 0)", index, f->call->Count());
                return nullptr;
            }
            *type = f->call->TypeOf(index);
            return f;
        }

        bool IsRef(Source2ModType t)
        {
            return t == Source2ModType::IntRef || t == Source2ModType::BoolRef || t == Source2ModType::FloatRef;
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

        cell_t IntOf(uint64_t bits, Source2ModType t)
        {
            switch (t)
            {
            case Source2ModType::Bool:
                return (bits & 0xFF) != 0;
            case Source2ModType::Int8:
                return static_cast<int8_t>(bits);
            case Source2ModType::Int16:
                return static_cast<int16_t>(bits);
            case Source2ModType::IntRef:
                return bits ? *reinterpret_cast<const int32_t*>(bits) : 0;
            case Source2ModType::BoolRef:
                return bits ? *reinterpret_cast<const bool*>(bits) : 0;
            case Source2ModType::FloatRef:
                return bits ? static_cast<cell_t>(*reinterpret_cast<const float*>(bits)) : 0;
            default:
                return static_cast<cell_t>(bits);
            }
        }

        float FloatOf(uint64_t bits, Source2ModType t)
        {
            if (t == Source2ModType::FloatRef)
                return bits ? *reinterpret_cast<const float*>(bits) : 0.0f;
            return t == Source2ModType::Double ? static_cast<float>(FromBits<double>(bits)) : FromBits<float>(bits);
        }

        // An out-parameter written in place; false for another type.
        bool SetRef(IPluginContext* ctx, uint64_t bits, Source2ModType t, cell_t value, bool isFloat)
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
            case Source2ModType::IntRef:
                *reinterpret_cast<int32_t*>(bits) = isFloat ? static_cast<int32_t>(sp_ctof(value)) : value;
                break;
            case Source2ModType::BoolRef:
                *reinterpret_cast<bool*>(bits) = value != 0;
                break;
            default:
                *reinterpret_cast<float*>(bits) = isFloat ? sp_ctof(value) : static_cast<float>(value);
                break;
            }
            return true;
        }

        cell_t ReturnString(IPluginContext* ctx, cell_t addr, cell_t maxlen, const std::string& text)
        {
            size_t written = 0;
            ctx->StringToLocalUTF8(addr, maxlen, text.c_str(), &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_ParamCount(IPluginContext* ctx, const cell_t* params)
        {
            Frame* f = FrameOf(ctx, params[1]);
            return f ? f->call->Count() : 0;
        }

        cell_t N_GetInt(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModType t;
            Frame* f = Param(ctx, params, &t);
            return f ? IntOf(f->call->Bits(params[2]), t) : 0;
        }

        cell_t N_GetFloat(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModType t;
            Frame* f = Param(ctx, params, &t);
            return f ? sp_ftoc(FloatOf(f->call->Bits(params[2]), t)) : 0;
        }

        cell_t N_GetEntity(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModType t;
            Frame* f = Param(ctx, params, &t);
            return f ? EntityCell(reinterpret_cast<CEntityInstance*>(f->call->Bits(params[2]))) : -1;
        }

        cell_t N_GetObject(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModType t;
            Frame* f = Param(ctx, params, &t);
            return f ? SchemaObjectForPointer(reinterpret_cast<void*>(f->call->Bits(params[2]))) : 0;
        }

        // GetString(index, buffer, maxlen): a string, a CBufferString, a command's text, an int64 as decimal text.
        cell_t N_GetString(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModType t;
            Frame* f = Param(ctx, params, &t);
            if (!f)
                return 0;
            const uint64_t bits = f->call->Bits(params[2]);
            std::string text;
            switch (t)
            {
            case Source2ModType::Int64:
                text = std::to_string(static_cast<int64_t>(bits));
                break;
            case Source2ModType::BufferString:
                if (const auto* b = reinterpret_cast<const CBufferString*>(bits))
                    text = b->Get();
                break;
            case Source2ModType::Command:
                if (const auto* c = reinterpret_cast<const CCommand*>(bits))
                    text = c->GetCommandString() ? c->GetCommandString() : "";
                break;
            case Source2ModType::String:
                if (const char* s = reinterpret_cast<const char*>(bits))
                    text = s;
                break;
            default:
                return ctx->ThrowNativeError("Parameter %d is not a string", params[2]);
            }
            return ReturnString(ctx, params[3], params[4], text);
        }

        cell_t N_GetVector(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModType t;
            Frame* f = Param(ctx, params, &t);
            if (!f)
                return 0;
            Vector v(0, 0, 0);
            if (t == Source2ModType::VectorValue)
                v = f->call->GetArg<Vector>(params[2]);
            else if (const auto* p = reinterpret_cast<const Vector*>(f->call->Bits(params[2])))
                v = *p;
            cell_t* out;
            ctx->LocalToPhysAddr(params[3], &out);
            out[0] = sp_ftoc(v.x);
            out[1] = sp_ftoc(v.y);
            out[2] = sp_ftoc(v.z);
            return 0;
        }

        cell_t N_SetInt(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModType t;
            if (Frame* f = Param(ctx, params, &t))
            {
                if (SetRef(ctx, f->call->Bits(params[2]), t, params[3], false))
                    return 0;
                f->call->SetBits(params[2], static_cast<uint64_t>(static_cast<int64_t>(t == Source2ModType::Bool ? (params[3] != 0) : params[3])));
            }
            return 0;
        }

        cell_t N_SetFloat(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModType t;
            if (Frame* f = Param(ctx, params, &t))
            {
                if (SetRef(ctx, f->call->Bits(params[2]), t, params[3], true))
                    return 0;
                f->call->SetBits(params[2], t == Source2ModType::Double ? ToBits<double>(sp_ctof(params[3])) : ToBits<float>(sp_ctof(params[3])));
            }
            return 0;
        }

        cell_t N_SetEntity(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModType t;
            if (Frame* f = Param(ctx, params, &t))
            {
                bool ok;
                CEntityInstance* e = OptionalEntityArg(ctx, params[3], &ok);
                if (ok)
                    f->call->SetBits(params[2], reinterpret_cast<uintptr_t>(e));
            }
            return 0;
        }

        cell_t N_SetObject(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModType t;
            if (Frame* f = Param(ctx, params, &t))
            {
                void* ptr = params[3] == 0 ? nullptr : SchemaObjectPointer(ctx, params[3]);
                if (params[3] == 0 || ptr)
                    f->call->SetBits(params[2], reinterpret_cast<uintptr_t>(ptr));
            }
            return 0;
        }

        // A Vector* parameter is written in place; a Vector by value is replaced.
        cell_t N_SetVector(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModType t;
            Frame* f = Param(ctx, params, &t);
            if (!f)
                return 0;
            cell_t* in;
            ctx->LocalToPhysAddr(params[3], &in);
            const Vector v(sp_ctof(in[0]), sp_ctof(in[1]), sp_ctof(in[2]));
            if (t == Source2ModType::VectorValue)
                f->call->SetArg<Vector>(params[2], v);
            else if (auto* p = reinterpret_cast<Vector*>(f->call->Bits(params[2])))
                *p = v;
            else
                return ctx->ThrowNativeError("The vector parameter is null");
            return 0;
        }

        // SetString(index, value): a String parameter points at a copy that
        // lives until the hooked call is over.
        cell_t N_SetString(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModType t;
            Frame* f = Param(ctx, params, &t);
            if (!f)
                return 0;
            if (t != Source2ModType::String)
                return ctx->ThrowNativeError("Parameter %d is not a String", params[2]);
            f->call->SetBits(params[2], reinterpret_cast<uintptr_t>(f->call->KeepString(LocalString(ctx, params[3]))));
            return 0;
        }

        // SetInt64(index, const char[] value): from decimal text.
        cell_t N_SetInt64(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModType t;
            Frame* f = Param(ctx, params, &t);
            if (!f)
                return 0;
            if (t != Source2ModType::Int64)
                return ctx->ThrowNativeError("Parameter %d is not an Int64", params[2]);
            f->call->SetBits(params[2], static_cast<uint64_t>(strtoll(LocalString(ctx, params[3]), nullptr, 10)));
            return 0;
        }

        cell_t N_SetBufferString(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModType t;
            Frame* f = Param(ctx, params, &t);
            if (!f)
                return 0;
            if (t != Source2ModType::BufferString)
                return ctx->ThrowNativeError("Parameter %d is not a BufferString", params[2]);
            auto* b = reinterpret_cast<CBufferString*>(f->call->Bits(params[2]));
            if (!b)
                return ctx->ThrowNativeError("The BufferString parameter is null");
            b->Clear();
            b->Insert(0, LocalString(ctx, params[3]));
            return 0;
        }

        cell_t N_GetCommandArgs(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModType t;
            Frame* f = Param(ctx, params, &t);
            if (!f)
                return 0;
            if (t != Source2ModType::Command)
                return ctx->ThrowNativeError("Parameter %d is not a Command", params[2]);
            const auto* c = reinterpret_cast<const CCommand*>(f->call->Bits(params[2]));
            if (!c)
                return 0;
            f->commands.push_back(std::make_unique<Source2ModCommandArgs>(c->ArgC(), c->ArgV(), c->ArgS(), c->GetCommandString()));
            const cell_t h = LendCommandArgs(f->commands.back().get(), f->owner);
            f->commandHandles.push_back(h);
            return h;
        }

        // ---- the return value ---------------------------------------------------------

        Frame* ReturnFrame(IPluginContext* ctx, cell_t handle, Source2ModType* type)
        {
            Frame* f = FrameOf(ctx, handle);
            if (!f)
                return nullptr;
            *type = f->call->Function()->GetReturnType();
            if (*type == Source2ModType::Void)
            {
                ctx->ThrowNativeError("The hooked function returns nothing");
                return nullptr;
            }
            return f;
        }

        cell_t N_RGetInt(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModType t;
            Frame* f = ReturnFrame(ctx, params[1], &t);
            return f ? IntOf(f->call->ret, t) : 0;
        }

        cell_t N_RGetFloat(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModType t;
            Frame* f = ReturnFrame(ctx, params[1], &t);
            return f ? sp_ftoc(FloatOf(f->call->ret, t)) : 0;
        }

        cell_t N_RGetEntity(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModType t;
            Frame* f = ReturnFrame(ctx, params[1], &t);
            return f ? EntityCell(reinterpret_cast<CEntityInstance*>(f->call->ret)) : -1;
        }

        cell_t N_RGetObject(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModType t;
            Frame* f = ReturnFrame(ctx, params[1], &t);
            return f ? SchemaObjectForPointer(reinterpret_cast<void*>(f->call->ret)) : 0;
        }

        // GetReturnString(buffer, maxlen): a const char* return value, or an int64 as decimal text.
        cell_t N_RGetString(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModType t;
            Frame* f = ReturnFrame(ctx, params[1], &t);
            if (!f)
                return 0;
            std::string text;
            if (t == Source2ModType::String)
            {
                if (const char* s = reinterpret_cast<const char*>(f->call->ret))
                    text = s;
            }
            else
                text = std::to_string(static_cast<int64_t>(f->call->ret));
            return ReturnString(ctx, params[2], params[3], text);
        }

        cell_t N_RSetInt(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModType t;
            if (Frame* f = ReturnFrame(ctx, params[1], &t))
                f->call->SetReturn<uint64_t>(static_cast<uint64_t>(static_cast<int64_t>(t == Source2ModType::Bool ? (params[2] != 0) : params[2])));
            return 0;
        }

        cell_t N_RSetFloat(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModType t;
            if (Frame* f = ReturnFrame(ctx, params[1], &t))
                f->call->SetReturn<uint64_t>(t == Source2ModType::Double ? ToBits<double>(sp_ctof(params[2])) : ToBits<float>(sp_ctof(params[2])));
            return 0;
        }

        cell_t N_RSetEntity(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModType t;
            if (Frame* f = ReturnFrame(ctx, params[1], &t))
            {
                bool ok;
                CEntityInstance* e = OptionalEntityArg(ctx, params[2], &ok);
                if (ok)
                    f->call->SetReturn<uintptr_t>(reinterpret_cast<uintptr_t>(e));
            }
            return 0;
        }

        cell_t N_RSetObject(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModType t;
            if (Frame* f = ReturnFrame(ctx, params[1], &t))
            {
                void* ptr = params[2] == 0 ? nullptr : SchemaObjectPointer(ctx, params[2]);
                if (params[2] == 0 || ptr)
                    f->call->SetReturn<uintptr_t>(reinterpret_cast<uintptr_t>(ptr));
            }
            return 0;
        }

        cell_t N_RSetInt64(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModType t;
            if (Frame* f = ReturnFrame(ctx, params[1], &t))
                f->call->SetReturn<uint64_t>(static_cast<uint64_t>(strtoll(LocalString(ctx, params[2]), nullptr, 10)));
            return 0;
        }

        // CallOriginal() (pre): the original now, past every hook, with the
        // current parameters; its value becomes the return value (as a cell).
        cell_t N_CallOriginal(IPluginContext* ctx, const cell_t* params)
        {
            Frame* f = FrameOf(ctx, params[1]);
            if (!f)
                return 0;
            if (f->call->post)
                return ctx->ThrowNativeError("CallOriginal is for pre handlers (in post the original has run)");
            uint64_t value = 0;
            if (!f->call->CallOriginalNow(&value))
                return ctx->ThrowNativeError("The original function is not available");
            const Source2ModType t = f->call->Function()->GetReturnType();
            switch (t)
            {
            case Source2ModType::Void:
                return 0;
            case Source2ModType::Float:
            case Source2ModType::Double:
                return sp_ftoc(FloatOf(value, t));
            case Source2ModType::Entity:
                return EntityCell(reinterpret_cast<CEntityInstance*>(value));
            case Source2ModType::Object:
            case Source2ModType::Pointer:
                return SchemaObjectForPointer(reinterpret_cast<void*>(value));
            default:
                return IntOf(value, t);
            }
        }

        const sp_nativeinfo_t g_functionHookNatives[] = {
            { "Source2ModHooks.HookFunction", N_HookFunction },
            { "Source2ModHooks.HookFunctionInstance", N_HookFunctionInstance },
            { "Source2ModHooks.HookFunctionClass", N_HookFunctionClass },
            { "FunctionContext.ParamCount.get", N_ParamCount },
            { "FunctionContext.GetInt", N_GetInt },
            { "FunctionContext.GetBool", N_GetInt },
            { "FunctionContext.GetFloat", N_GetFloat },
            { "FunctionContext.GetEntity", N_GetEntity },
            { "FunctionContext.GetObject", N_GetObject },
            { "FunctionContext.GetString", N_GetString },
            { "FunctionContext.GetVector", N_GetVector },
            { "FunctionContext.SetInt", N_SetInt },
            { "FunctionContext.SetBool", N_SetInt },
            { "FunctionContext.SetFloat", N_SetFloat },
            { "FunctionContext.SetEntity", N_SetEntity },
            { "FunctionContext.SetObject", N_SetObject },
            { "FunctionContext.SetVector", N_SetVector },
            { "FunctionContext.SetString", N_SetString },
            { "FunctionContext.SetInt64", N_SetInt64 },
            { "FunctionContext.SetBufferString", N_SetBufferString },
            { "FunctionContext.GetCommandArgs", N_GetCommandArgs },
            { "FunctionContext.GetReturnInt", N_RGetInt },
            { "FunctionContext.GetReturnBool", N_RGetInt },
            { "FunctionContext.GetReturnFloat", N_RGetFloat },
            { "FunctionContext.GetReturnEntity", N_RGetEntity },
            { "FunctionContext.GetReturnObject", N_RGetObject },
            { "FunctionContext.GetReturnString", N_RGetString },
            { "FunctionContext.SetReturnInt", N_RSetInt },
            { "FunctionContext.SetReturnBool", N_RSetInt },
            { "FunctionContext.SetReturnFloat", N_RSetFloat },
            { "FunctionContext.SetReturnEntity", N_RSetEntity },
            { "FunctionContext.SetReturnObject", N_RSetObject },
            { "FunctionContext.SetReturnInt64", N_RSetInt64 },
            { "FunctionContext.CallOriginal", N_CallOriginal },
            { nullptr, nullptr },
        };
    }

    bool UnhookFunctionHook(PluginId owner, Source2ModHookId id)
    {
        auto it = g_hooks.find(id);
        if (it == g_hooks.end() || it->second != owner)
            return false;
        g_hooks.erase(it);
        return hooks::hooksManager.Unhook(id);
    }

    void ForgetFunctionHooks(PluginId id)
    {
        std::vector<Source2ModHookId> ids;
        for (const auto& [hookId, owner] : g_hooks)
            if (owner == id)
                ids.push_back(hookId);
        for (Source2ModHookId hookId : ids)
        {
            g_hooks.erase(hookId);
            hooks::hooksManager.Unhook(hookId);
        }
    }

    const sp_nativeinfo_t* GetRawHookNatives()
    {
        return g_functionHookNatives;
    }
}
