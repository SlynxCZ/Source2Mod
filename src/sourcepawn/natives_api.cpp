/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * Commands and ConVars of the SourcePawn API mirroring the C++ one
 * (source2mod/commands.inc, source2mod/convars.inc): Source2ModCommands on
 * the commands manager, CConVar / CConVarRef / Source2ModConVars on the
 * ConVars manager. Everything is registered under the calling script's id,
 * so the managers drop it when the script unloads.
 *
 * Project: Source2Mod
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/commands.h"
#include "core/convars.h"

#include <sp_callable.h>

#include <algorithm>
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
            return static_cast<ISource2ModCommands&>(commands::commandsManager).UnregisterConCommand(static_cast<Source2ModHookId>(params[1]));
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
