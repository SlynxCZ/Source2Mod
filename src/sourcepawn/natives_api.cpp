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
 * Commands and ConVars of the SourcePawn API mirroring the C++ one
 * (source2mod/commands.inc, source2mod/convars.inc): Source2ModCommands on
 * the commands manager, CConVar / CConVarRef / Source2ModConVars on the
 * ConVars manager. Everything is registered under the calling script's id,
 * so the managers drop it when the script unloads.
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/commands.h"
#include "core/convars.h"
#include "core/shared.h"

#include <source2mod/ISource2ModScheduler.h>

#include <sp_callable.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

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

        // ---- commands -------------------------------------------------------------

        // The context and the arguments of the command being run, as handles
        // the handler gets; freed when it returns.
        HandleType ContextType()
        {
            static const HandleType t = Handles().CreateType("Source2ModCommandContext", nullptr, 0, false);
            return t;
        }

        HandleType ArgsType()
        {
            static const HandleType t = Handles().CreateType("Source2ModCommandArgs", nullptr, 0, false);
            return t;
        }

        struct CommandCall
        {
            cell_t context;
            cell_t args;

            CommandCall(const Source2ModCommandContext& c, const Source2ModCommandArgs& a, PluginId owner)
            {
                context = Handles().Create(ContextType(), const_cast<Source2ModCommandContext*>(&c), owner);
                args = Handles().Create(ArgsType(), const_cast<Source2ModCommandArgs*>(&a), owner);
            }

            ~CommandCall()
            {
                Handles().Free(context, 0, true);
                Handles().Free(args, 0, true);
            }
        };

        const Source2ModCommandContext* ContextOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<const Source2ModCommandContext*>(ReadHandle(ctx, handle, ContextType()));
        }

        const Source2ModCommandArgs* ArgsOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<const Source2ModCommandArgs*>(ReadHandle(ctx, handle, ArgsType()));
        }

        ChatHandler MakeChatHandler(IPluginFunction* fn, PluginId owner)
        {
            return ChatHandler([fn, owner](const Source2ModCommandContext& c, const Source2ModCommandArgs& a, bool post)
            {
                CommandCall call(c, a, owner);
                sp::CallArgs args;
                args.PushCell(call.context);
                args.PushCell(call.args);
                args.PushCell(post ? 1 : 0);
                Invoke(fn, args);
            });
        }

        const char* Permission(IPluginContext* ctx, cell_t addr)
        {
            const char* p = LocalString(ctx, addr);
            return *p ? p : nullptr;
        }

        IPluginFunction* Function(IPluginContext* ctx, cell_t id)
        {
            IPluginFunction* fn = ctx->GetFunctionById(static_cast<funcid_t>(id));
            if (!fn)
                ctx->ThrowNativeError("Invalid function id (%X)", id);
            return fn;
        }

        // RegisterConCommand(name, Source2ModChatHandler handler, permission = "")
        cell_t N_RegisterConCommand(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[2]);
            if (!fn)
                return 0;
            const PluginId owner = CallerId(ctx);
            return static_cast<cell_t>(commands::commandsManager.RegisterConCommand(owner, LocalString(ctx, params[1]),
                                                                                    MakeChatHandler(fn, owner), Permission(ctx, params[3])));
        }

        cell_t N_RegisterChatListener(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[2]);
            if (!fn)
                return 0;
            const PluginId owner = CallerId(ctx);
            return static_cast<cell_t>(commands::commandsManager.RegisterChatListener(owner, LocalString(ctx, params[1]),
                                                                                      MakeChatHandler(fn, owner), Permission(ctx, params[3])));
        }

        // RegisterConListener(name, Source2ModCommandHandler handler, bool post)
        cell_t N_RegisterConListener(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[2]);
            if (!fn)
                return 0;
            const PluginId owner = CallerId(ctx);
            CommandHandler handler([fn, owner](const Source2ModCommandContext& c, const Source2ModCommandArgs& a, bool post) -> Action
            {
                CommandCall call(c, a, owner);
                sp::CallArgs args;
                args.PushCell(call.context);
                args.PushCell(call.args);
                args.PushCell(post ? 1 : 0);
                cell_t res = 0;
                Invoke(fn, args, &res);
                return static_cast<Action>(std::clamp<cell_t>(res, 0, 2));
            });
            return static_cast<cell_t>(commands::commandsManager.RegisterConListener(owner, LocalString(ctx, params[1]), std::move(handler), params[3] != 0));
        }

        cell_t N_Unregister(IPluginContext*, const cell_t* params)
        {
            return static_cast<ISource2ModCommands&>(commands::commandsManager).UnregisterConCommand(static_cast<Source2ModCommandId>(params[1]));
        }

        // ReplyToCommand(context, format, ...)
        cell_t N_ReplyToCommand(IPluginContext* ctx, const cell_t* params)
        {
            const Source2ModCommandContext* c = ContextOf(ctx, params[1]);
            char msg[1024];
            if (c && FormatParams(ctx, params, 2, msg, sizeof(msg)))
                commands::commandsManager.ReplyToCommand(*c, msg);
            return 0;
        }

        cell_t N_CtxGetPlayerSlot(IPluginContext* ctx, const cell_t* params)
        {
            const Source2ModCommandContext* c = ContextOf(ctx, params[1]);
            return c ? c->GetPlayerSlot().Get() : -1;
        }

        cell_t N_CtxGetCommandTarget(IPluginContext* ctx, const cell_t* params)
        {
            const Source2ModCommandContext* c = ContextOf(ctx, params[1]);
            return c ? c->GetCommandTarget() : 0;
        }

        cell_t N_CtxGetSource(IPluginContext* ctx, const cell_t* params)
        {
            const Source2ModCommandContext* c = ContextOf(ctx, params[1]);
            return c ? static_cast<cell_t>(c->GetSource()) : 0;
        }

        cell_t N_CtxIsFromChat(IPluginContext* ctx, const cell_t* params)
        {
            const Source2ModCommandContext* c = ContextOf(ctx, params[1]);
            return c && c->IsFromChat();
        }

        cell_t N_ArgsArgC(IPluginContext* ctx, const cell_t* params)
        {
            const Source2ModCommandArgs* a = ArgsOf(ctx, params[1]);
            return a ? a->ArgC() : 0;
        }

        cell_t ToBuffer(IPluginContext* ctx, cell_t addr, cell_t maxlen, const char* value)
        {
            size_t written = 0;
            ctx->StringToLocalUTF8(addr, maxlen, value ? value : "", &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_ArgsArg(IPluginContext* ctx, const cell_t* params)
        {
            const Source2ModCommandArgs* a = ArgsOf(ctx, params[1]);
            return a ? ToBuffer(ctx, params[3], params[4], a->Arg(params[2])) : 0;
        }

        cell_t N_ArgsArgS(IPluginContext* ctx, const cell_t* params)
        {
            const Source2ModCommandArgs* a = ArgsOf(ctx, params[1]);
            return a ? ToBuffer(ctx, params[2], params[3], a->ArgS()) : 0;
        }

        cell_t N_ArgsGetCommandString(IPluginContext* ctx, const cell_t* params)
        {
            const Source2ModCommandArgs* a = ArgsOf(ctx, params[1]);
            return a ? ToBuffer(ctx, params[2], params[3], a->GetCommandString()) : 0;
        }

        // ---- ConVars ----------------------------------------------------------------

        // What ConVar_Register gives a C++ plugin's ConVars.
        constexpr uint64 kRegisterFlags = FCVAR_RELEASE | FCVAR_GAMEDLL;

        cell_t Existing(const char* name)
        {
            const uint16 index = convars::convarsManager.GetConvarAccessIndexByName(name);
            return index == SOURCE2MOD_INVALID_CONVAR_INDEX ? 0 : ConVarHandle(index);
        }

        cell_t Created(IPluginContext* ctx, const char* name, uint16 index)
        {
            if (index == SOURCE2MOD_INVALID_CONVAR_INDEX)
                return ctx->ThrowNativeError("Could not create ConVar \"%s\"", name);
            return ConVarHandle(index);
        }

        // CreateInt(name, flags, help, def, hasMin = false, min = 0, hasMax = false, max = 0)
        cell_t N_CreateInt(IPluginContext* ctx, const cell_t* params)
        {
            const char* name = LocalString(ctx, params[1]);
            if (cell_t h = Existing(name))
                return h;
            int32 def = params[4], min = params[6], max = params[8];
            return Created(ctx, name, convars::convarsManager.CreateConVar(name, EConVarType_Int32, LocalString(ctx, params[3]),
                                                                           static_cast<uint32_t>(params[2]) | kRegisterFlags,
                                                                           params[5] != 0, params[7] != 0, &def, &min, &max));
        }

        cell_t N_CreateFloat(IPluginContext* ctx, const cell_t* params)
        {
            const char* name = LocalString(ctx, params[1]);
            if (cell_t h = Existing(name))
                return h;
            float def = sp_ctof(params[4]), min = sp_ctof(params[6]), max = sp_ctof(params[8]);
            return Created(ctx, name, convars::convarsManager.CreateConVar(name, EConVarType_Float32, LocalString(ctx, params[3]),
                                                                           static_cast<uint32_t>(params[2]) | kRegisterFlags,
                                                                           params[5] != 0, params[7] != 0, &def, &min, &max));
        }

        cell_t N_CreateBool(IPluginContext* ctx, const cell_t* params)
        {
            const char* name = LocalString(ctx, params[1]);
            if (cell_t h = Existing(name))
                return h;
            bool def = params[4] != 0;
            return Created(ctx, name, convars::convarsManager.CreateConVar(name, EConVarType_Bool, LocalString(ctx, params[3]),
                                                                           static_cast<uint32_t>(params[2]) | kRegisterFlags,
                                                                           false, false, &def, nullptr, nullptr));
        }

        cell_t N_CreateString(IPluginContext* ctx, const cell_t* params)
        {
            const char* name = LocalString(ctx, params[1]);
            if (cell_t h = Existing(name))
                return h;
            const std::string def = LocalString(ctx, params[4]);
            return Created(ctx, name, convars::convarsManager.CreateConVar(name, EConVarType_String, LocalString(ctx, params[3]),
                                                                           static_cast<uint32_t>(params[2]) | kRegisterFlags,
                                                                           false, false, const_cast<char*>(def.c_str()), nullptr, nullptr));
        }

        cell_t N_RefFind(IPluginContext* ctx, const cell_t* params)
        {
            return Existing(LocalString(ctx, params[1]));
        }

        // The access index a ConVar handle holds (+1), as in natives_convars.cpp.
        bool IndexOf(cell_t handle, uint16* index)
        {
            void* object = Handles().Read(handle, Types().conVar);
            if (!object)
                return false;
            *index = static_cast<uint16>(reinterpret_cast<uintptr_t>(object) - 1);
            return true;
        }

        cell_t N_RefIsValidRef(IPluginContext*, const cell_t* params)
        {
            uint16 index;
            return IndexOf(params[1], &index) && ConVarRefAbstract(index).IsValidRef();
        }

        cell_t N_RefIsConVarDataAvailable(IPluginContext*, const cell_t* params)
        {
            uint16 index;
            if (!IndexOf(params[1], &index))
                return 0;
            ConVarRefAbstract ref(index);
            return ref.IsValidRef() && ref.IsConVarDataAvailable();
        }

        // HookConVarChange(handler(ConVar convar, int slot, newValue, oldValue))
        cell_t N_HookConVarChange(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[1]);
            if (!fn)
                return 0;
            ConVarChangeHandler handler([fn](ConVarRefAbstract* ref, CSplitScreenSlot slot, const char* newValue, const char* oldValue)
            {
                if (!ref)
                    return;
                sp::CallArgs args;
                args.PushCell(ConVarHandle(ref->GetAccessIndex()));
                args.PushCell(slot.Get());
                args.PushString(newValue ? newValue : "");
                args.PushString(oldValue ? oldValue : "");
                Invoke(fn, args);
            });
            return static_cast<cell_t>(convars::convarsManager.HookConVarChange(CallerId(ctx), std::move(handler)));
        }

        cell_t N_UnhookConVarChange(IPluginContext*, const cell_t* params)
        {
            return convars::convarsManager.UnhookConVarChangeId(static_cast<Source2ModHookId>(params[1]));
        }

        // ---- Source2ModTimer: the scheduler's Timer fields -------------------------

        cell_t N_TimerInterval(IPluginContext* ctx, const cell_t* params)
        {
            Timer* t = ScriptTimer(ctx, params[1]);
            return sp_ftoc(t ? t->Interval : 0.0f);
        }

        cell_t N_TimerExecTime(IPluginContext* ctx, const cell_t* params)
        {
            Timer* t = ScriptTimer(ctx, params[1]);
            return sp_ftoc(t ? static_cast<float>(t->ExecTime) : 0.0f);
        }

        cell_t N_TimerFlags(IPluginContext* ctx, const cell_t* params)
        {
            Timer* t = ScriptTimer(ctx, params[1]);
            return t ? t->Flags : 0;
        }

        cell_t N_TimerInExec(IPluginContext* ctx, const cell_t* params)
        {
            Timer* t = ScriptTimer(ctx, params[1]);
            return t && t->InExec;
        }

        cell_t N_TimerKillMe(IPluginContext* ctx, const cell_t* params)
        {
            Timer* t = ScriptTimer(ctx, params[1]);
            return !t || t->KillMe;
        }

        // ---- CGlobalVars (ISource2ModAPI::GetGlobalVars) ------------------------------

        CGlobalVars* Globals(IPluginContext* ctx)
        {
            CGlobalVars* globals = shared::getGlobalVars();
            if (!globals)
                ctx->ThrowNativeError("The global variables are not available yet");
            return globals;
        }

#define S2M_GLOBAL(field, conv)                                                    \
        cell_t N_Globals_##field(IPluginContext* ctx, const cell_t*)               \
        {                                                                          \
            CGlobalVars* g = Globals(ctx);                                         \
            return g ? conv(g->field) : 0;                                         \
        }
#define S2M_CELL(v) static_cast<cell_t>(v)
        S2M_GLOBAL(realtime, sp_ftoc)
        S2M_GLOBAL(framecount, S2M_CELL)
        S2M_GLOBAL(absoluteframetime, sp_ftoc)
        S2M_GLOBAL(absoluteframetime_unbounded, sp_ftoc)
        S2M_GLOBAL(maxClients, S2M_CELL)
        S2M_GLOBAL(m_nCurrentTickThisFrame, S2M_CELL)
        S2M_GLOBAL(m_nTotalTicksThisFrame, S2M_CELL)
        S2M_GLOBAL(curtime, sp_ftoc)
        S2M_GLOBAL(frametime, sp_ftoc)
        S2M_GLOBAL(interpolation_amount, sp_ftoc)
        S2M_GLOBAL(m_bInSimulation, S2M_CELL)
        S2M_GLOBAL(tickcount, S2M_CELL)
        S2M_GLOBAL(m_flSubtickFraction, sp_ftoc)
        S2M_GLOBAL(eLoadType, S2M_CELL)
        S2M_GLOBAL(mp_teamplay, S2M_CELL)
        S2M_GLOBAL(maxEntities, S2M_CELL)
        S2M_GLOBAL(serverCount, S2M_CELL)
#undef S2M_CELL
#undef S2M_GLOBAL

        template <string_t CGlobalVars::*FIELD>
        cell_t N_GlobalsString(IPluginContext* ctx, const cell_t* params)
        {
            CGlobalVars* g = Globals(ctx);
            return ToBuffer(ctx, params[2], params[3], g ? (g->*FIELD).ToCStr() : "");
        }

        // ---- the rest of ISource2ModConVars: typed values, flags, create, delete -------

        // Through the interface, whose overloads have the slot default.
        ISource2ModConVars& CV()
        {
            return convars::convarsManager;
        }

        bool IndexArg(IPluginContext* ctx, cell_t handle, uint16* index)
        {
            if (IndexOf(handle, index))
                return true;
            ctx->ThrowNativeError("Invalid ConVar handle %x", handle);
            return false;
        }

        cell_t N_CVGetType(IPluginContext* ctx, const cell_t* params)
        {
            uint16 index;
            return IndexArg(ctx, params[1], &index) ? CV().GetType(index) : EConVarType_Invalid;
        }

        // GetFlags(ConVar, int flags[2]) / SetFlags(ConVar, const int flags[2]): all 64 bits.
        cell_t N_CVGetFlags(IPluginContext* ctx, const cell_t* params)
        {
            uint16 index;
            if (!IndexArg(ctx, params[1], &index))
                return 0;
            const uint64 flags = CV().GetFlags(index);
            cell_t* out;
            ctx->LocalToPhysAddr(params[2], &out);
            out[0] = static_cast<cell_t>(flags & 0xFFFFFFFFu);
            out[1] = static_cast<cell_t>(flags >> 32);
            return 0;
        }

        cell_t N_CVSetFlags(IPluginContext* ctx, const cell_t* params)
        {
            uint16 index;
            if (!IndexArg(ctx, params[1], &index))
                return 0;
            cell_t* in;
            ctx->LocalToPhysAddr(params[2], &in);
            CV().SetFlags(index, static_cast<uint32_t>(in[0]) | (static_cast<uint64>(static_cast<uint32_t>(in[1])) << 32));
            return 0;
        }

        cell_t N_CVGetDouble(IPluginContext* ctx, const cell_t* params)
        {
            uint16 index;
            return IndexArg(ctx, params[1], &index) ? sp_ftoc(static_cast<float>(CV().GetDouble(index))) : 0;
        }

        cell_t N_CVSetDouble(IPluginContext* ctx, const cell_t* params)
        {
            uint16 index;
            if (IndexArg(ctx, params[1], &index))
                CV().SetDouble(index, sp_ctof(params[2]));
            return 0;
        }

        void FloatsOut(IPluginContext* ctx, cell_t addr, const float* v, int n)
        {
            cell_t* out;
            ctx->LocalToPhysAddr(addr, &out);
            for (int i = 0; i < n; i++)
                out[i] = sp_ftoc(v[i]);
        }

        void FloatsIn(IPluginContext* ctx, cell_t addr, float* v, int n)
        {
            cell_t* in;
            ctx->LocalToPhysAddr(addr, &in);
            for (int i = 0; i < n; i++)
                v[i] = sp_ctof(in[i]);
        }

        // GetVector2 / GetVector3 / GetVector4 / GetQAngle (ConVar, float[N])
        template <typename V, int N, V (ISource2ModConVars::*GET)(uint16, CSplitScreenSlot)>
        cell_t N_CVGetFloats(IPluginContext* ctx, const cell_t* params)
        {
            uint16 index;
            if (!IndexArg(ctx, params[1], &index))
                return 0;
            const V v = (CV().*GET)(index, CSplitScreenSlot(-1));
            FloatsOut(ctx, params[2], reinterpret_cast<const float*>(&v), N);
            return 0;
        }

        template <typename V, int N, void (ISource2ModConVars::*SET)(uint16, const V&, CSplitScreenSlot)>
        cell_t N_CVSetFloats(IPluginContext* ctx, const cell_t* params)
        {
            uint16 index;
            if (!IndexArg(ctx, params[1], &index))
                return 0;
            V v;
            FloatsIn(ctx, params[2], reinterpret_cast<float*>(&v), N);
            (CV().*SET)(index, v, CSplitScreenSlot(-1));
            return 0;
        }

        cell_t N_CVGetColor(IPluginContext* ctx, const cell_t* params)
        {
            uint16 index;
            if (!IndexArg(ctx, params[1], &index))
                return 0;
            const Color c = CV().GetColor(index);
            cell_t* out;
            ctx->LocalToPhysAddr(params[2], &out);
            out[0] = c.r();
            out[1] = c.g();
            out[2] = c.b();
            out[3] = c.a();
            return 0;
        }

        cell_t N_CVSetColor(IPluginContext* ctx, const cell_t* params)
        {
            uint16 index;
            if (!IndexArg(ctx, params[1], &index))
                return 0;
            cell_t* in;
            ctx->LocalToPhysAddr(params[2], &in);
            CV().SetColor(index, Color(in[0], in[1], in[2], in[3]));
            return 0;
        }

        cell_t N_CVDeleteConVar(IPluginContext* ctx, const cell_t* params)
        {
            uint16 index;
            if (IndexArg(ctx, params[1], &index))
                CV().DeleteConVar(index);
            return 0;
        }

        // A ConVar value of `type` read from text, laid out as CreateConVar wants it.
        struct ConVarValue
        {
            alignas(16) uint8_t bytes[32] = {};
            std::string text;

            bool Parse(EConVarType type, const char* s)
            {
                float f[4] = {};
                switch (type)
                {
                case EConVarType_Bool:
                    *reinterpret_cast<bool*>(bytes) = std::strtol(s, nullptr, 10) != 0 || std::strcmp(s, "true") == 0;
                    return true;
                case EConVarType_Int16:
                    *reinterpret_cast<int16*>(bytes) = static_cast<int16>(std::strtol(s, nullptr, 10));
                    return true;
                case EConVarType_UInt16:
                    *reinterpret_cast<uint16*>(bytes) = static_cast<uint16>(std::strtoul(s, nullptr, 10));
                    return true;
                case EConVarType_Int32:
                    *reinterpret_cast<int32*>(bytes) = static_cast<int32>(std::strtol(s, nullptr, 10));
                    return true;
                case EConVarType_UInt32:
                    *reinterpret_cast<uint32*>(bytes) = static_cast<uint32>(std::strtoul(s, nullptr, 10));
                    return true;
                case EConVarType_Int64:
                    *reinterpret_cast<int64*>(bytes) = std::strtoll(s, nullptr, 10);
                    return true;
                case EConVarType_UInt64:
                    *reinterpret_cast<uint64*>(bytes) = std::strtoull(s, nullptr, 10);
                    return true;
                case EConVarType_Float32:
                    *reinterpret_cast<float*>(bytes) = std::strtof(s, nullptr);
                    return true;
                case EConVarType_Float64:
                    *reinterpret_cast<double*>(bytes) = std::strtod(s, nullptr);
                    return true;
                case EConVarType_String:
                    text = s;
                    return true;
                case EConVarType_Color:
                {
                    int c[4] = { 0, 0, 0, 255 };
                    std::sscanf(s, "%d %d %d %d", &c[0], &c[1], &c[2], &c[3]);
                    *reinterpret_cast<Color*>(bytes) = Color(c[0], c[1], c[2], c[3]);
                    return true;
                }
                case EConVarType_Vector2:
                case EConVarType_Vector3:
                case EConVarType_Vector4:
                case EConVarType_Qangle:
                case EConVarType_VectorWS:
                    std::sscanf(s, "%f %f %f %f", &f[0], &f[1], &f[2], &f[3]);
                    std::memcpy(bytes, f, sizeof(f));
                    return true;
                default:
                    return false;
                }
            }

            void* Pointer()
            {
                return text.empty() ? static_cast<void*>(bytes) : static_cast<void*>(text.data());
            }
        };

        // CConVar.Create(name, type, flags, help, defaultValue, hasMin, min, hasMax, max): any
        // EConVarType, the values as text ("1 2 3" for a vector, "r g b a" for a color).
        cell_t N_CVCreate(IPluginContext* ctx, const cell_t* params)
        {
            const char* name = LocalString(ctx, params[1]);
            if (cell_t h = Existing(name))
                return h;
            const auto type = static_cast<EConVarType>(params[2]);
            ConVarValue def, min, max;
            if (!def.Parse(type, LocalString(ctx, params[5])))
                return ctx->ThrowNativeError("Invalid ConVar type %d", params[2]);
            const bool hasMin = params[6] != 0 && type != EConVarType_String;
            const bool hasMax = params[8] != 0 && type != EConVarType_String;
            if (hasMin)
                min.Parse(type, LocalString(ctx, params[7]));
            if (hasMax)
                max.Parse(type, LocalString(ctx, params[9]));
            // An empty string default still needs a valid char*.
            void* defValue = type == EConVarType_String ? static_cast<void*>(def.text.data()) : def.Pointer();
            return Created(ctx, name, CV().CreateConVar(name, type, LocalString(ctx, params[4]), static_cast<uint32_t>(params[3]) | kRegisterFlags,
                                                        hasMin, hasMax, defValue, hasMin ? min.Pointer() : nullptr, hasMax ? max.Pointer() : nullptr));
        }

        const sp_nativeinfo_t g_apiNatives[] = {
            // source2mod/commands.inc
            { "Source2ModCommands.RegisterConCommand", N_RegisterConCommand },
            { "Source2ModCommands.UnregisterConCommand", N_Unregister },
            { "Source2ModCommands.RegisterChatListener", N_RegisterChatListener },
            { "Source2ModCommands.UnregisterChatListener", N_Unregister },
            { "Source2ModCommands.RegisterConListener", N_RegisterConListener },
            { "Source2ModCommands.UnregisterConListener", N_Unregister },
            { "Source2ModCommands.ReplyToCommand", N_ReplyToCommand },
            { "Source2ModCommandContext.GetPlayerSlot", N_CtxGetPlayerSlot },
            { "Source2ModCommandContext.GetCommandTarget", N_CtxGetCommandTarget },
            { "Source2ModCommandContext.GetSource", N_CtxGetSource },
            { "Source2ModCommandContext.IsFromChat", N_CtxIsFromChat },
            { "Source2ModCommandArgs.ArgC", N_ArgsArgC },
            { "Source2ModCommandArgs.Arg", N_ArgsArg },
            { "Source2ModCommandArgs.ArgS", N_ArgsArgS },
            { "Source2ModCommandArgs.GetCommandString", N_ArgsGetCommandString },
            // source2mod/convars.inc
            { "CConVar.CreateInt", N_CreateInt },
            { "CConVar.CreateFloat", N_CreateFloat },
            { "CConVar.CreateBool", N_CreateBool },
            { "CConVar.CreateString", N_CreateString },
            { "CConVarRef.Find", N_RefFind },
            { "CConVarRef.IsValidRef", N_RefIsValidRef },
            { "CConVarRef.IsConVarDataAvailable", N_RefIsConVarDataAvailable },
            { "Source2ModConVars.HookConVarChange", N_HookConVarChange },
            { "Source2ModConVars.UnhookConVarChange", N_UnhookConVarChange },
            { "CConVar.Create", N_CVCreate },
            // source2mod/scheduler.inc
            { "Source2ModTimer.Interval.get", N_TimerInterval },
            { "Source2ModTimer.ExecTime.get", N_TimerExecTime },
            { "Source2ModTimer.Flags.get", N_TimerFlags },
            { "Source2ModTimer.InExec.get", N_TimerInExec },
            { "Source2ModTimer.KillMe.get", N_TimerKillMe },
            // source2mod/api.inc
            { "CGlobalVars.realtime.get", N_Globals_realtime },
            { "CGlobalVars.framecount.get", N_Globals_framecount },
            { "CGlobalVars.absoluteframetime.get", N_Globals_absoluteframetime },
            { "CGlobalVars.absoluteframetime_unbounded.get", N_Globals_absoluteframetime_unbounded },
            { "CGlobalVars.maxClients.get", N_Globals_maxClients },
            { "CGlobalVars.m_nCurrentTickThisFrame.get", N_Globals_m_nCurrentTickThisFrame },
            { "CGlobalVars.m_nTotalTicksThisFrame.get", N_Globals_m_nTotalTicksThisFrame },
            { "CGlobalVars.curtime.get", N_Globals_curtime },
            { "CGlobalVars.frametime.get", N_Globals_frametime },
            { "CGlobalVars.interpolation_amount.get", N_Globals_interpolation_amount },
            { "CGlobalVars.m_bInSimulation.get", N_Globals_m_bInSimulation },
            { "CGlobalVars.tickcount.get", N_Globals_tickcount },
            { "CGlobalVars.m_flSubtickFraction.get", N_Globals_m_flSubtickFraction },
            { "CGlobalVars.eLoadType.get", N_Globals_eLoadType },
            { "CGlobalVars.mp_teamplay.get", N_Globals_mp_teamplay },
            { "CGlobalVars.maxEntities.get", N_Globals_maxEntities },
            { "CGlobalVars.serverCount.get", N_Globals_serverCount },
            { "CGlobalVars.GetMapName", N_GlobalsString<&CGlobalVars::mapname> },
            { "CGlobalVars.GetStartSpot", N_GlobalsString<&CGlobalVars::startspot> },
            { "Source2ModConVars.GetType", N_CVGetType },
            { "Source2ModConVars.GetFlags", N_CVGetFlags },
            { "Source2ModConVars.SetFlags", N_CVSetFlags },
            { "Source2ModConVars.GetDouble", N_CVGetDouble },
            { "Source2ModConVars.SetDouble", N_CVSetDouble },
            { "Source2ModConVars.GetVector2", N_CVGetFloats<Vector2D, 2, &ISource2ModConVars::GetVector2> },
            { "Source2ModConVars.GetVector3", N_CVGetFloats<Vector, 3, &ISource2ModConVars::GetVector3> },
            { "Source2ModConVars.GetVector4", N_CVGetFloats<Vector4D, 4, &ISource2ModConVars::GetVector4> },
            { "Source2ModConVars.GetQAngle", N_CVGetFloats<QAngle, 3, &ISource2ModConVars::GetQAngle> },
            { "Source2ModConVars.GetColor", N_CVGetColor },
            { "Source2ModConVars.SetVector2", N_CVSetFloats<Vector2D, 2, &ISource2ModConVars::SetVector2> },
            { "Source2ModConVars.SetVector3", N_CVSetFloats<Vector, 3, &ISource2ModConVars::SetVector3> },
            { "Source2ModConVars.SetVector4", N_CVSetFloats<Vector4D, 4, &ISource2ModConVars::SetVector4> },
            { "Source2ModConVars.SetQAngle", N_CVSetFloats<QAngle, 3, &ISource2ModConVars::SetQAngle> },
            { "Source2ModConVars.SetColor", N_CVSetColor },
            { "Source2ModConVars.DeleteConVar", N_CVDeleteConVar },
            { nullptr, nullptr },
        };
    }

    cell_t LendCommandArgs(const Source2ModCommandArgs* args, PluginId owner)
    {
        return Handles().Create(ArgsType(), const_cast<Source2ModCommandArgs*>(args), owner);
    }

    cell_t LendCommandContext(const Source2ModCommandContext* context, PluginId owner)
    {
        return Handles().Create(ContextType(), const_cast<Source2ModCommandContext*>(context), owner);
    }

    const sp_nativeinfo_t* GetApiNatives()
    {
        return g_apiNatives;
    }
}
