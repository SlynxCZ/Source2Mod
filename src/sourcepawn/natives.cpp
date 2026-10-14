/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * The core's SourcePawn natives. Names, parameters and behaviour are
 * SourceMod's (AlliedModders LLC) as its includes declare them; what is behind
 * them is Source2Mod's commands, events, scheduler and player access. Clients
 * are SourceMod's 1-based indices: client = player slot + 1, 0 = the server.
 *
 * Project: Source2Mod
 */
#include "natives.h"
#include "scriptplugin.h"

#include "core/commands.h"
#include "core/events.h"
#include "core/pluginmanager.h"
#include "core/shared.h"
#include "utils/log.h"

#include <source2mod/schema/entity/classes/CCSPlayerController.h>

#include <base-runtime.h>
#include <sp_callable.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <vector>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        // ---- helpers ------------------------------------------------------------

        ScriptPlugin* Plugin(IPluginContext* ctx)
        {
            return ScriptPlugin::FromContext(ctx);
        }

        const char* Str(IPluginContext* ctx, cell_t addr)
        {
            char* s = nullptr;
            if (ctx->LocalToString(addr, &s) != SP_ERROR_NONE || !s)
                return "";
            return s;
        }

        CCSPlayerController* Controller(int client)
        {
            if (client < 1 || client > kMaxClients)
                return nullptr;
            return CCSPlayerController::FromSlot(client - 1);
        }

        bool IsConnected(CCSPlayerController* player)
        {
            if (!player)
                return false;
            const PlayerConnectedState state = player->m_iConnected();
            return state == PlayerConnectedState::Connected || state == PlayerConnectedState::Connecting
                || state == PlayerConnectedState::Reconnecting;
        }

        bool IsInGame(CCSPlayerController* player)
        {
            return player && player->m_iConnected() == PlayerConnectedState::Connected;
        }

        // Natives that take a client check it the way SourceMod does.
        CCSPlayerController* CheckClient(IPluginContext* ctx, int client, bool inGame)
        {
            CCSPlayerController* player = Controller(client);
            if (!player || !IsConnected(player))
            {
                ctx->ReportError("Client index %d is invalid", client);
                return nullptr;
            }
            if (inGame && !IsInGame(player))
            {
                ctx->ReportError("Client %d is not in game", client);
                return nullptr;
            }
            return player;
        }

        int UserIdOf(int client)
        {
            if (!g_pEngineServer)
                return -1;
            return g_pEngineServer->GetPlayerUserId(CPlayerSlot(client - 1)).Get();
        }

        // name<userid><STEAM_1:Y:Z><>, SourceMod's %L.
        std::string LogName(int client)
        {
            if (client == 0)
                return "Console<0><Console><Console>";
            CCSPlayerController* player = Controller(client);
            if (!player)
                return "<unknown>";
            const uint32_t account = static_cast<uint32_t>(player->m_steamID() & 0xFFFFFFFFu);
            char buf[256];
            snprintf(buf, sizeof(buf), "%s<%d><STEAM_1:%u:%u><>", player->m_iszPlayerName(), UserIdOf(client),
                     account & 1u, account >> 1);
            return buf;
        }

        // Format into a local buffer from the given parameter on.
        bool FormatTo(IPluginContext* ctx, const cell_t* params, int fmtParam, std::string& out)
        {
            char buf[2048];
            if (!FormatParams(ctx, params, fmtParam, buf, sizeof(buf)))
                return false;
            out = buf;
            return true;
        }

        // ---- the command being dispatched (GetCmdArg & co.) -------------------

        struct CommandFrame
        {
            const Source2ModCommandContext* ctx;
            const Source2ModCommandArgs* args;
            int client;
        };
        std::vector<CommandFrame> g_commandStack;

        // ---- the event being dispatched (Event handles) ------------------------

        struct EventFrame
        {
            IGameEvent* event;
            bool* dontBroadcast;
        };
        std::vector<EventFrame> g_eventStack;

        // Event handles carry this tag; the rest is the frame index. They are
        // valid only inside the callback they were handed to.
        constexpr cell_t kEventHandleTag = 0x7E000000;

        IGameEvent* EventOf(IPluginContext* ctx, cell_t handle, bool** dontBroadcast = nullptr)
        {
            const cell_t index = handle - kEventHandleTag;
            if ((handle & 0x7F000000) != kEventHandleTag || index < 0 || index >= static_cast<cell_t>(g_eventStack.size()))
            {
                ctx->ReportError("Invalid game event handle %x", handle);
                return nullptr;
            }
            if (dontBroadcast)
                *dontBroadcast = g_eventStack[index].dontBroadcast;
            return g_eventStack[index].event;
        }

        // UnhookEvent finds the hook HookEvent made by what it was made with.
        std::map<std::tuple<ScriptPlugin*, std::string, funcid_t, bool>, Source2ModHookId> g_eventHooks;

        const auto g_engineStart = std::chrono::steady_clock::now();
    }

    // ---- format -------------------------------------------------------------------

    bool FormatParams(IPluginContext* ctx, const cell_t* params, int fmtParam, char* out, size_t maxlen, size_t* written)
    {
        if (maxlen)
            out[0] = '\0';

        char* fmt = nullptr;
        if (ctx->LocalToString(params[fmtParam], &fmt) != SP_ERROR_NONE || !fmt)
            return false;

        const int numParams = params[0];
        int arg = fmtParam + 1;
        std::string result;

        for (const char* p = fmt; *p; ++p)
        {
            if (*p != '%')
            {
                result += *p;
                continue;
            }

            ++p;
            if (*p == '%')
            {
                result += '%';
                continue;
            }

            std::string spec = "%";
            while (*p == '-' || *p == '0' || *p == '+' || *p == ' ')
                spec += *p++;
            while (isdigit(static_cast<unsigned char>(*p)))
                spec += *p++;
            if (*p == '.')
            {
                spec += *p++;
                while (isdigit(static_cast<unsigned char>(*p)))
                    spec += *p++;
            }

            const char c = *p;
            if (!c)
                break;

            if (arg > numParams)
            {
                ctx->ReportError("String formatted incorrectly - parameter %d (total %d)", arg, numParams);
                return false;
            }

            cell_t* value = nullptr;
            if (c != 's' && ctx->LocalToPhysAddr(params[arg], &value) != SP_ERROR_NONE)
                return false;

            char piece[1024];
            switch (c)
            {
                case 'd':
                case 'i':
                    snprintf(piece, sizeof(piece), (spec + "d").c_str(), static_cast<int>(*value));
                    break;
                case 'u':
                    snprintf(piece, sizeof(piece), (spec + "u").c_str(), static_cast<unsigned>(*value));
                    break;
                case 'x':
                case 'X':
                    snprintf(piece, sizeof(piece), (spec + c).c_str(), static_cast<unsigned>(*value));
                    break;
                case 'f':
                    snprintf(piece, sizeof(piece), (spec + "f").c_str(), static_cast<double>(sp_ctof(*value)));
                    break;
                case 'c':
                    snprintf(piece, sizeof(piece), (spec + "c").c_str(), static_cast<int>(*value));
                    break;
                case 'b':
                {
                    std::string bits;
                    const auto v = static_cast<uint32_t>(*value);
                    for (int i = 31; i >= 0; --i)
                        if (!bits.empty() || (v >> i) & 1u || i == 0)
                            bits += ((v >> i) & 1u) ? '1' : '0';
                    snprintf(piece, sizeof(piece), (spec + "s").c_str(), bits.c_str());
                    break;
                }
                case 's':
                    snprintf(piece, sizeof(piece), (spec + "s").c_str(), Str(ctx, params[arg]));
                    break;
                case 'N':
                case 'L':
                {
                    const int client = *value;
                    std::string text;
                    if (c == 'L')
                        text = LogName(client);
                    else if (client == 0)
                        text = "Console";
                    else if (CCSPlayerController* player = Controller(client))
                        text = player->m_iszPlayerName();
                    else
                    {
                        ctx->ReportError("Client index %d is invalid", client);
                        return false;
                    }
                    snprintf(piece, sizeof(piece), (spec + "s").c_str(), text.c_str());
                    break;
                }
                case 't':
                case 'T':
                    ctx->ReportError("Translations (%%%c) are not supported yet", c);
                    return false;
                default:
                    ctx->ReportError("Invalid format specifier '%c'", c);
                    return false;
            }

            result += piece;
            ++arg;
        }

        if (maxlen)
        {
            const size_t n = std::min(result.size(), maxlen - 1);
            memcpy(out, result.data(), n);
            out[n] = '\0';
            if (written)
                *written = n;
        }
        return true;
    }

    namespace
    {
        // ---- core ---------------------------------------------------------------

        cell_t N_MarkNativeAsOptional(IPluginContext* ctx, const cell_t* params)
        {
            // The native table is the runtime's (sp::BaseRuntime), not part
            // of the public context interface.
            uint32_t idx;
            auto* runtime = static_cast<sp::BaseRuntime*>(ctx->GetRuntime());
            if (runtime->FindNativeByName(Str(ctx, params[1]), &idx) == SP_ERROR_NONE)
                const_cast<sp_native_t*>(runtime->GetNative(idx))->flags |= SP_NTVFLAG_OPTIONAL;
            return 0;
        }

        cell_t N_VerifyCoreVersion(IPluginContext*, const cell_t*)
        {
            return 7;    // SOURCE2MOD_PLUGINAPI_VERSION
        }

        cell_t N_IsNullString(IPluginContext* ctx, const cell_t* params)
        {
            char* s = nullptr;
            ctx->LocalToStringNULL(params[1], &s);
            return s == nullptr;
        }

        cell_t N_IsNullVector(IPluginContext* ctx, const cell_t* params)
        {
            cell_t* addr = nullptr;
            ctx->LocalToPhysAddr(params[1], &addr);
            return addr == ctx->GetNullRef(SP_NULL_VECTOR);
        }

        cell_t N_ThrowError(IPluginContext* ctx, const cell_t* params)
        {
            std::string msg;
            if (FormatTo(ctx, params, 1, msg))
                ctx->ReportError("%s", msg.c_str());
            return 0;
        }

        // SourceMod pauses the plugin; here it is unloaded, between frames.
        cell_t N_SetFailState(IPluginContext* ctx, const cell_t* params)
        {
            std::string msg;
            if (!FormatTo(ctx, params, 1, msg))
                return 0;

            if (ScriptPlugin* plugin = Plugin(ctx))
            {
                FP_ERROR("Plugin {} failed: {}", plugin->FileName(), msg);
                pluginManager.RequestUnload(plugin->Id());
            }
            ctx->ReportFatalError("%s", msg.c_str());
            return 0;
        }

        // ---- console / logging --------------------------------------------------

        cell_t N_PrintToServer(IPluginContext* ctx, const cell_t* params)
        {
            std::string msg;
            if (FormatTo(ctx, params, 1, msg))
                ConMsg("%s\n", msg.c_str());
            return 0;
        }

        cell_t N_PrintToConsole(IPluginContext* ctx, const cell_t* params)
        {
            std::string msg;
            const int client = params[1];
            if (client == 0)
            {
                if (FormatTo(ctx, params, 2, msg))
                    ConMsg("%s\n", msg.c_str());
                return 0;
            }
            CCSPlayerController* player = CheckClient(ctx, client, true);
            if (player && FormatTo(ctx, params, 2, msg))
                player->PrintToConsole(msg.c_str());
            return 0;
        }

        cell_t N_ServerCommand(IPluginContext* ctx, const cell_t* params)
        {
            std::string msg;
            if (FormatTo(ctx, params, 1, msg) && g_pEngineServer)
                g_pEngineServer->ServerCommand((msg + "\n").c_str());
            return 0;
        }

        cell_t N_LogMessage(IPluginContext* ctx, const cell_t* params)
        {
            std::string msg;
            if (FormatTo(ctx, params, 1, msg))
                FP_INFO("[{}] {}", Plugin(ctx) ? Plugin(ctx)->FileName() : "?", msg);
            return 0;
        }

        cell_t N_LogError(IPluginContext* ctx, const cell_t* params)
        {
            std::string msg;
            if (FormatTo(ctx, params, 1, msg))
                FP_ERROR("[{}] {}", Plugin(ctx) ? Plugin(ctx)->FileName() : "?", msg);
            return 0;
        }

        // ---- commands -----------------------------------------------------------

        // ConCmd: Action (int client, int args); SrvCmd: Action (int args).
        cell_t RegisterCommand(IPluginContext* ctx, const cell_t* params, bool serverOnly, const char* permission)
        {
            ScriptPlugin* plugin = Plugin(ctx);
            IPluginFunction* fn = ctx->GetFunctionById(params[2]);
            if (!plugin || !fn)
                return ctx->ThrowNativeError("Invalid function id (%X)", params[2]);

            const char* name = Str(ctx, params[1]);

            ChatHandler handler([fn, serverOnly](const Source2ModCommandContext& c, const Source2ModCommandArgs& a, bool post)
            {
                if (post)
                    return;

                const CPlayerSlot slot = c.GetPlayerSlot();
                const int client = slot.IsValid() ? slot.Get() + 1 : 0;
                if (serverOnly && client != 0)
                    return;

                g_commandStack.push_back({ &c, &a, client });

                sp::CallArgs args;
                if (!serverOnly)
                    args.PushCell(client);
                args.PushCell(a.ArgC() > 0 ? a.ArgC() - 1 : 0);
                fn->Invoke(args, nullptr);

                g_commandStack.pop_back();
            });

            commands::commandsManager.RegisterConCommand(plugin->Id(), name, std::move(handler), permission);
            return 0;
        }

        cell_t N_RegConsoleCmd(IPluginContext* ctx, const cell_t* params)
        {
            return RegisterCommand(ctx, params, false, nullptr);
        }

        cell_t N_RegServerCmd(IPluginContext* ctx, const cell_t* params)
        {
            return RegisterCommand(ctx, params, true, nullptr);
        }

        // SourceMod's admin flags have no direct counterpart in Source2Mod's
        // dotted permissions; each flag becomes "admin.<flag>" (the lowest set
        // bit decides), which the "admin" group of permissions.json covers.
        cell_t N_RegAdminCmd(IPluginContext* ctx, const cell_t* params)
        {
            static const char* const kFlags[] = {
                "reservation", "generic", "kick", "ban", "unban", "slay", "changemap", "convars",
                "config", "chat", "vote", "password", "rcon", "cheats", "root",
                "custom1", "custom2", "custom3", "custom4", "custom5", "custom6",
            };

            const auto flags = static_cast<uint32_t>(params[3]);
            std::string permission;
            for (size_t i = 0; i < std::size(kFlags); i++)
            {
                if (flags & (1u << i))
                {
                    permission = std::string("admin.") + kFlags[i];
                    break;
                }
            }

            return RegisterCommand(ctx, params, false, permission.empty() ? nullptr : permission.c_str());
        }

        cell_t N_GetCmdArgs(IPluginContext* ctx, const cell_t*)
        {
            if (g_commandStack.empty())
                return ctx->ThrowNativeError("No command is being processed");
            const int argc = g_commandStack.back().args->ArgC();
            return argc > 0 ? argc - 1 : 0;
        }

        cell_t N_GetCmdArg(IPluginContext* ctx, const cell_t* params)
        {
            if (g_commandStack.empty())
                return ctx->ThrowNativeError("No command is being processed");
            size_t written = 0;
            ctx->StringToLocalUTF8(params[2], params[3], g_commandStack.back().args->Arg(params[1]), &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_GetCmdArgString(IPluginContext* ctx, const cell_t* params)
        {
            if (g_commandStack.empty())
                return ctx->ThrowNativeError("No command is being processed");
            size_t written = 0;
            ctx->StringToLocalUTF8(params[1], params[2], g_commandStack.back().args->ArgS(), &written);
            return static_cast<cell_t>(written);
        }

        // Into the console or chat the command came from, as SourceMod does.
        cell_t N_ReplyToCommand(IPluginContext* ctx, const cell_t* params)
        {
            std::string msg;
            if (!FormatTo(ctx, params, 2, msg))
                return 0;

            const int client = params[1];
            if (!g_commandStack.empty() && g_commandStack.back().client == client)
            {
                commands::commandsManager.ReplyToCommand(*g_commandStack.back().ctx, msg.c_str());
                return 0;
            }

            if (client == 0)
                ConMsg("%s\n", msg.c_str());
            else if (CCSPlayerController* player = CheckClient(ctx, client, true))
                player->PrintToConsole(msg.c_str());
            return 0;
        }

        // ---- timers -------------------------------------------------------------

        cell_t N_CreateTimer(IPluginContext* ctx, const cell_t* params)
        {
            ScriptPlugin* plugin = Plugin(ctx);
            IPluginFunction* fn = ctx->GetFunctionById(params[2]);
            if (!plugin || !fn)
                return ctx->ThrowNativeError("Invalid function id (%X)", params[2]);

            return plugin->AddTimer(sp_ctof(params[1]), fn, params[3], params[4]);
        }

        cell_t N_KillTimer(IPluginContext* ctx, const cell_t* params)
        {
            ScriptPlugin* plugin = Plugin(ctx);
            if (!plugin || !plugin->KillTimer(params[1]))
                return ctx->ThrowNativeError("Invalid timer handle %x", params[1]);
            return 0;
        }

        // CloseHandle / delete: timers are the only Handles a script owns so far.
        cell_t N_CloseHandle(IPluginContext* ctx, const cell_t* params)
        {
            if (params[1] == 0)
                return 0;
            if (ScriptPlugin* plugin = Plugin(ctx))
                plugin->KillTimer(params[1]);
            return 0;
        }

        // ---- events -------------------------------------------------------------

        // EventHookMode_Pre = 0, EventHookMode_Post, EventHookMode_PostNoCopy
        cell_t N_HookEvent(IPluginContext* ctx, const cell_t* params)
        {
            ScriptPlugin* plugin = Plugin(ctx);
            IPluginFunction* fn = ctx->GetFunctionById(params[2]);
            if (!plugin || !fn)
                return ctx->ThrowNativeError("Invalid function id (%X)", params[2]);

            const std::string name = Str(ctx, params[1]);
            const int mode = params[3];
            const bool post = mode != 0;
            const bool noCopy = mode == 2;

            GameEventHandler handler([fn, name, noCopy](IGameEvent* event, bool post, bool& dontBroadcast) -> Action
            {
                cell_t handle = 0;
                if (!noCopy)
                {
                    handle = kEventHandleTag + static_cast<cell_t>(g_eventStack.size());
                    g_eventStack.push_back({ event, &dontBroadcast });
                }

                sp::CallArgs args;
                args.PushCell(handle);
                args.PushString(name.c_str());
                args.PushCell(dontBroadcast ? 1 : 0);

                cell_t res = kPluginContinue;
                fn->Invoke(args, &res);

                if (!noCopy)
                    g_eventStack.pop_back();

                // Handled in a pre hook keeps the event from being fired.
                if (!post && res >= kPluginHandled)
                    return Action::Supersede;
                return Action::Ignore;
            });

            const Source2ModHookId id = events::eventManager.HookGameEvent(plugin->Id(), name.c_str(), std::move(handler), post);
            if (!id)
                return ctx->ThrowNativeError("Game event \"%s\" does not exist", name.c_str());

            g_eventHooks[{ plugin, name, params[2], post }] = id;
            return 0;
        }

        cell_t N_UnhookEvent(IPluginContext* ctx, const cell_t* params)
        {
            ScriptPlugin* plugin = Plugin(ctx);
            const auto key = std::make_tuple(plugin, std::string(Str(ctx, params[1])), static_cast<funcid_t>(params[2]), params[3] != 0);
            auto it = g_eventHooks.find(key);
            if (it == g_eventHooks.end())
                return ctx->ThrowNativeError("Game event \"%s\" has no active hook", Str(ctx, params[1]));

            events::eventManager.Unhook(it->second);
            g_eventHooks.erase(it);
            return 0;
        }

        cell_t N_EventGetBool(IPluginContext* ctx, const cell_t* params)
        {
            IGameEvent* ev = EventOf(ctx, params[1]);
            return ev ? ev->GetBool(Str(ctx, params[2]), params[3] != 0) : 0;
        }

        cell_t N_EventGetInt(IPluginContext* ctx, const cell_t* params)
        {
            IGameEvent* ev = EventOf(ctx, params[1]);
            return ev ? ev->GetInt(Str(ctx, params[2]), params[3]) : 0;
        }

        cell_t N_EventGetFloat(IPluginContext* ctx, const cell_t* params)
        {
            IGameEvent* ev = EventOf(ctx, params[1]);
            return ev ? sp_ftoc(ev->GetFloat(Str(ctx, params[2]), sp_ctof(params[3]))) : 0;
        }

        cell_t N_EventGetString(IPluginContext* ctx, const cell_t* params)
        {
            IGameEvent* ev = EventOf(ctx, params[1]);
            if (ev)
                ctx->StringToLocalUTF8(params[3], params[4], ev->GetString(Str(ctx, params[2]), Str(ctx, params[5])), nullptr);
            return 0;
        }

        cell_t N_EventSetBool(IPluginContext* ctx, const cell_t* params)
        {
            if (IGameEvent* ev = EventOf(ctx, params[1]))
                ev->SetBool(Str(ctx, params[2]), params[3] != 0);
            return 0;
        }

        cell_t N_EventSetInt(IPluginContext* ctx, const cell_t* params)
        {
            if (IGameEvent* ev = EventOf(ctx, params[1]))
                ev->SetInt(Str(ctx, params[2]), params[3]);
            return 0;
        }

        cell_t N_EventSetFloat(IPluginContext* ctx, const cell_t* params)
        {
            if (IGameEvent* ev = EventOf(ctx, params[1]))
                ev->SetFloat(Str(ctx, params[2]), sp_ctof(params[3]));
            return 0;
        }

        cell_t N_EventSetString(IPluginContext* ctx, const cell_t* params)
        {
            if (IGameEvent* ev = EventOf(ctx, params[1]))
                ev->SetString(Str(ctx, params[2]), Str(ctx, params[3]));
            return 0;
        }

        cell_t N_EventGetName(IPluginContext* ctx, const cell_t* params)
        {
            if (IGameEvent* ev = EventOf(ctx, params[1]))
                ctx->StringToLocalUTF8(params[2], params[3], ev->GetName(), nullptr);
            return 0;
        }

        cell_t N_EventSetBroadcast(IPluginContext* ctx, const cell_t* params)
        {
            bool* dontBroadcast = nullptr;
            if (EventOf(ctx, params[1], &dontBroadcast) && dontBroadcast)
                *dontBroadcast = params[2] != 0;
            return 0;
        }

        cell_t N_EventGetBroadcastDisabled(IPluginContext* ctx, const cell_t* params)
        {
            bool* dontBroadcast = nullptr;
            return EventOf(ctx, params[1], &dontBroadcast) && dontBroadcast && *dontBroadcast;
        }

        // ---- clients ------------------------------------------------------------

        cell_t N_IsClientConnected(IPluginContext*, const cell_t* params)
        {
            return IsConnected(Controller(params[1]));
        }

        cell_t N_IsClientInGame(IPluginContext*, const cell_t* params)
        {
            return IsInGame(Controller(params[1]));
        }

        cell_t N_IsFakeClient(IPluginContext* ctx, const cell_t* params)
        {
            CCSPlayerController* player = CheckClient(ctx, params[1], false);
            return player && player->IsBot();
        }

        cell_t N_IsClientAuthorized(IPluginContext* ctx, const cell_t* params)
        {
            CCSPlayerController* player = CheckClient(ctx, params[1], false);
            return player && g_pEngineServer && g_pEngineServer->IsClientFullyAuthenticated(CPlayerSlot(params[1] - 1));
        }

        cell_t N_GetClientName(IPluginContext* ctx, const cell_t* params)
        {
            if (params[1] == 0)
            {
                ctx->StringToLocalUTF8(params[2], params[3], "Console", nullptr);
                return 1;
            }
            CCSPlayerController* player = CheckClient(ctx, params[1], false);
            if (!player)
                return 0;
            ctx->StringToLocalUTF8(params[2], params[3], player->m_iszPlayerName(), nullptr);
            return 1;
        }

        cell_t N_GetClientUserId(IPluginContext* ctx, const cell_t* params)
        {
            return CheckClient(ctx, params[1], false) ? UserIdOf(params[1]) : 0;
        }

        cell_t N_GetClientOfUserId(IPluginContext*, const cell_t* params)
        {
            for (int client = 1; client <= kMaxClients; client++)
                if (IsConnected(Controller(client)) && UserIdOf(client) == params[1])
                    return client;
            return 0;
        }

        cell_t N_GetSteamAccountID(IPluginContext* ctx, const cell_t* params)
        {
            CCSPlayerController* player = CheckClient(ctx, params[1], false);
            return player ? static_cast<cell_t>(player->m_steamID() & 0xFFFFFFFFu) : 0;
        }

        cell_t N_GetClientCount(IPluginContext*, const cell_t* params)
        {
            const bool inGameOnly = params[1] != 0;
            cell_t count = 0;
            for (int client = 1; client <= kMaxClients; client++)
            {
                CCSPlayerController* player = Controller(client);
                if (inGameOnly ? IsInGame(player) : IsConnected(player))
                    count++;
            }
            return count;
        }

        cell_t N_GetMaxHumanPlayers(IPluginContext*, const cell_t*)
        {
            return kMaxClients;
        }

        cell_t N_PrintToChat(IPluginContext* ctx, const cell_t* params)
        {
            CCSPlayerController* player = CheckClient(ctx, params[1], true);
            std::string msg;
            if (player && FormatTo(ctx, params, 2, msg))
                player->PrintToChat(msg.c_str());
            return 0;
        }

        cell_t N_PrintToChatAll(IPluginContext* ctx, const cell_t* params)
        {
            std::string msg;
            if (!FormatTo(ctx, params, 1, msg))
                return 0;
            for (int client = 1; client <= kMaxClients; client++)
            {
                CCSPlayerController* player = Controller(client);
                if (IsInGame(player) && !player->IsBot())
                    player->PrintToChat(msg.c_str());
            }
            return 0;
        }

        cell_t N_PrintCenterText(IPluginContext* ctx, const cell_t* params)
        {
            CCSPlayerController* player = CheckClient(ctx, params[1], true);
            std::string msg;
            if (player && FormatTo(ctx, params, 2, msg))
                player->PrintToCenter(msg.c_str());
            return 0;
        }

        cell_t N_GetGameTime(IPluginContext*, const cell_t*)
        {
            CGlobalVars* globals = shared::getGlobalVars();
            return sp_ftoc(globals ? globals->curtime : 0.0f);
        }

        cell_t N_GetEngineTime(IPluginContext*, const cell_t*)
        {
            const std::chrono::duration<float> elapsed = std::chrono::steady_clock::now() - g_engineStart;
            return sp_ftoc(elapsed.count());
        }

        // ---- strings ------------------------------------------------------------

        cell_t N_Format(IPluginContext* ctx, const cell_t* params)
        {
            char* buffer = nullptr;
            if (ctx->LocalToString(params[1], &buffer) != SP_ERROR_NONE)
                return 0;

            // Formatting into a buffer that is also an argument: work on a copy.
            std::string tmp(static_cast<size_t>(std::max(params[2], 1)), '\0');
            size_t written = 0;
            if (!FormatParams(ctx, params, 3, tmp.data(), tmp.size(), &written))
                return 0;
            ctx->StringToLocalUTF8(params[1], params[2], tmp.c_str(), &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_strlen(IPluginContext* ctx, const cell_t* params)
        {
            return static_cast<cell_t>(strlen(Str(ctx, params[1])));
        }

        cell_t N_strcopy(IPluginContext* ctx, const cell_t* params)
        {
            size_t written = 0;
            const std::string src = Str(ctx, params[3]);
            ctx->StringToLocalUTF8(params[1], params[2], src.c_str(), &written);
            return static_cast<cell_t>(written);
        }

        int Compare(const char* a, const char* b, size_t n, bool caseSensitive)
        {
            if (caseSensitive)
                return n ? strncmp(a, b, n) : strcmp(a, b);
#ifdef _WIN32
            return n ? _strnicmp(a, b, n) : _stricmp(a, b);
#else
            return n ? strncasecmp(a, b, n) : strcasecmp(a, b);
#endif
        }

        cell_t N_strcmp(IPluginContext* ctx, const cell_t* params)
        {
            return Compare(Str(ctx, params[1]), Str(ctx, params[2]), 0, params[3] != 0);
        }

        cell_t N_strncmp(IPluginContext* ctx, const cell_t* params)
        {
            return Compare(Str(ctx, params[1]), Str(ctx, params[2]), static_cast<size_t>(std::max(params[3], 0)), params[4] != 0);
        }

        cell_t N_StrContains(IPluginContext* ctx, const cell_t* params)
        {
            std::string str = Str(ctx, params[1]);
            std::string sub = Str(ctx, params[2]);
            if (params[3] == 0)
            {
                auto lower = [](std::string& s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); }); };
                lower(str);
                lower(sub);
            }
            const size_t pos = str.find(sub);
            return pos == std::string::npos ? -1 : static_cast<cell_t>(pos);
        }

        cell_t N_IntToString(IPluginContext* ctx, const cell_t* params)
        {
            size_t written = 0;
            ctx->StringToLocalUTF8(params[2], params[3], std::to_string(params[1]).c_str(), &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_StringToInt(IPluginContext* ctx, const cell_t* params)
        {
            return static_cast<cell_t>(strtol(Str(ctx, params[1]), nullptr, params[2]));
        }

        cell_t N_FloatToString(IPluginContext* ctx, const cell_t* params)
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "%f", static_cast<double>(sp_ctof(params[1])));
            size_t written = 0;
            ctx->StringToLocalUTF8(params[2], params[3], buf, &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_StringToFloat(IPluginContext* ctx, const cell_t* params)
        {
            return sp_ftoc(strtof(Str(ctx, params[1]), nullptr));
        }

        const sp_nativeinfo_t g_natives[] = {
            // core.inc
            { "MarkNativeAsOptional", N_MarkNativeAsOptional },
            { "VerifyCoreVersion", N_VerifyCoreVersion },
            { "IsNullString", N_IsNullString },
            { "IsNullVector", N_IsNullVector },
            // functions.inc / halflife.inc
            { "ThrowError", N_ThrowError },
            { "SetFailState", N_SetFailState },
            { "GetGameTime", N_GetGameTime },
            { "GetEngineTime", N_GetEngineTime },
            // console.inc / logging.inc
            { "PrintToServer", N_PrintToServer },
            { "PrintToConsole", N_PrintToConsole },
            { "ServerCommand", N_ServerCommand },
            { "LogMessage", N_LogMessage },
            { "LogError", N_LogError },
            { "RegConsoleCmd", N_RegConsoleCmd },
            { "RegServerCmd", N_RegServerCmd },
            { "RegAdminCmd", N_RegAdminCmd },
            { "GetCmdArgs", N_GetCmdArgs },
            { "GetCmdArg", N_GetCmdArg },
            { "GetCmdArgString", N_GetCmdArgString },
            { "ReplyToCommand", N_ReplyToCommand },
            // timers.inc / handles.inc
            { "CreateTimer", N_CreateTimer },
            { "KillTimer", N_KillTimer },
            { "CloseHandle", N_CloseHandle },
            { "Handle.~Handle", N_CloseHandle },
            { "Handle.Close", N_CloseHandle },
            // events.inc
            { "HookEvent", N_HookEvent },
            { "UnhookEvent", N_UnhookEvent },
            { "GetEventBool", N_EventGetBool },
            { "GetEventInt", N_EventGetInt },
            { "GetEventFloat", N_EventGetFloat },
            { "GetEventString", N_EventGetString },
            { "SetEventBool", N_EventSetBool },
            { "SetEventInt", N_EventSetInt },
            { "SetEventFloat", N_EventSetFloat },
            { "SetEventString", N_EventSetString },
            { "GetEventName", N_EventGetName },
            { "SetEventBroadcast", N_EventSetBroadcast },
            { "Event.GetBool", N_EventGetBool },
            { "Event.GetInt", N_EventGetInt },
            { "Event.GetFloat", N_EventGetFloat },
            { "Event.GetString", N_EventGetString },
            { "Event.SetBool", N_EventSetBool },
            { "Event.SetInt", N_EventSetInt },
            { "Event.SetFloat", N_EventSetFloat },
            { "Event.SetString", N_EventSetString },
            { "Event.GetName", N_EventGetName },
            { "Event.BroadcastDisabled.set", N_EventSetBroadcast },
            { "Event.BroadcastDisabled.get", N_EventGetBroadcastDisabled },
            // clients.inc / halflife.inc
            { "IsClientConnected", N_IsClientConnected },
            { "IsClientInGame", N_IsClientInGame },
            { "IsFakeClient", N_IsFakeClient },
            { "IsClientAuthorized", N_IsClientAuthorized },
            { "GetClientName", N_GetClientName },
            { "GetClientUserId", N_GetClientUserId },
            { "GetClientOfUserId", N_GetClientOfUserId },
            { "GetSteamAccountID", N_GetSteamAccountID },
            { "GetClientCount", N_GetClientCount },
            { "GetMaxHumanPlayers", N_GetMaxHumanPlayers },
            { "PrintToChat", N_PrintToChat },
            { "PrintToChatAll", N_PrintToChatAll },
            { "PrintCenterText", N_PrintCenterText },
            // string.inc
            { "Format", N_Format },
            { "strlen", N_strlen },
            { "strcopy", N_strcopy },
            { "strcmp", N_strcmp },
            { "strncmp", N_strncmp },
            { "StrContains", N_StrContains },
            { "IntToString", N_IntToString },
            { "StringToInt", N_StringToInt },
            { "FloatToString", N_FloatToString },
            { "StringToFloat", N_StringToFloat },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetCoreNatives()
    {
        return g_natives;
    }
}
