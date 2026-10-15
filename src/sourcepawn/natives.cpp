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
#include "handles.h"
#include "scriptplugin.h"

#include "core/commands.h"
#include "core/events.h"
#include "core/pluginmanager.h"
#include "core/scheduler.h"
#include "core/translations.h"
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

        // An Event handle points at one of these on the hook's stack; it is
        // freed when the callback returns.
        struct EventFrame
        {
            IGameEvent* event;
            bool* dontBroadcast;
        };

        IGameEvent* EventOf(IPluginContext* ctx, cell_t handle, bool** dontBroadcast = nullptr)
        {
            auto* frame = static_cast<EventFrame*>(ReadHandle(ctx, handle, Types().event));
            if (!frame)
                return nullptr;
            if (dontBroadcast)
                *dontBroadcast = frame->dontBroadcast;
            return frame->event;
        }

        // UnhookEvent finds the hook HookEvent made by what it was made with.
        std::map<std::tuple<PluginId, std::string, funcid_t, bool>, Source2ModHookId> g_eventHooks;

        const auto g_engineStart = std::chrono::steady_clock::now();
    }

    const char* LocalString(IPluginContext* ctx, cell_t addr)
    {
        return Str(ctx, addr);
    }

    void ForgetEventHooks(PluginId id)
    {
        // The hooks themselves went with eventManager.RemoveAllForPlugin().
        std::erase_if(g_eventHooks, [id](const auto& entry) { return std::get<0>(entry.first) == id; });
    }

    // ---- format -------------------------------------------------------------------

    namespace
    {
        // Whom %t translates for: a player slot, -1 for the server.
        int g_formatTarget = -1;

        bool FormatInto(IPluginContext* ctx, const char* fmt, const cell_t* params, int& arg, std::string& result, int depth);
    }

    void SetFormatTarget(int slot)
    {
        g_formatTarget = slot;
    }

    bool TranslateInto(IPluginContext* ctx, const cell_t* params, int& arg, int target, const char* name, std::string& result, int depth)
    {
        ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
        auto& tr = translations::translationsManager;
        const translations::Phrase* phrase = plugin ? tr.Find(plugin->Id(), name) : nullptr;
        const std::string* text = phrase ? tr.TextFor(*phrase, CPlayerSlot(target)) : nullptr;
        // A phrase not there (yet: a script may push it later) shows as its name.
        if (!text)
        {
            result += name;
            return true;
        }

        const int count = phrase->format.empty() ? 0 : static_cast<int>(phrase->format.size()) - 1;
        const int first = arg;
        if (first + count - 1 > params[0])
        {
            ctx->ReportError("Translation \"%s\" needs %d more argument(s)", name, first + count - 1 - params[0]);
            return false;
        }
        arg += count;

        for (const char* p = text->c_str(); *p; ++p)
        {
            if (*p == '{' && isdigit(static_cast<unsigned char>(p[1])))
            {
                char* end = nullptr;
                const long index = std::strtol(p + 1, &end, 10);
                if (*end == '}' && index >= 1 && index <= count)
                {
                    const std::string spec = "%" + (phrase->format[index].empty() ? std::string("s") : phrase->format[index]);
                    const cell_t one[2] = { 1, params[first + index - 1] };
                    int a = 1;
                    if (!FormatInto(ctx, spec.c_str(), one, a, result, depth + 1))
                        return false;
                    p = end;
                    continue;
                }
            }
            result += *p;
        }
        return true;
    }

    bool FormatParams(IPluginContext* ctx, const cell_t* params, int fmtParam, char* out, size_t maxlen, size_t* written)
    {
        if (maxlen)
            out[0] = '\0';

        char* fmt = nullptr;
        if (ctx->LocalToString(params[fmtParam], &fmt) != SP_ERROR_NONE || !fmt)
            return false;

        int arg = fmtParam + 1;
        std::string result;
        if (!FormatInto(ctx, fmt, params, arg, result, 0))
            return false;

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
    bool FormatInto(IPluginContext* ctx, const char* fmt, const cell_t* params, int& arg, std::string& result, int depth)
    {
        if (depth > 8)
        {
            ctx->ReportError("Translations nest too deeply");
            return false;
        }
        const int numParams = params[0];

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
                {
                    // %t: the phrase for the format target; %T: phrase, then the
                    // target's player slot (-1 = the server).
                    const char* name = Str(ctx, params[arg]);
                    int target = g_formatTarget;
                    ++arg;
                    if (c == 'T')
                    {
                        if (arg > numParams)
                        {
                            ctx->ReportError("String formatted incorrectly - parameter %d (total %d)", arg, numParams);
                            return false;
                        }
                        cell_t* t = nullptr;
                        if (ctx->LocalToPhysAddr(params[arg], &t) != SP_ERROR_NONE)
                            return false;
                        target = *t;
                        ++arg;
                    }
                    if (!TranslateInto(ctx, params, arg, target, name, result, depth))
                        return false;
                    continue;
                }
                default:
                    ctx->ReportError("Invalid format specifier '%c'", c);
                    return false;
            }

            result += piece;
            ++arg;
        }
        return true;
    }
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

        // Source2ModApi.ConPrint(const char[] message): as it is, no formatting.
        cell_t N_ConPrint(IPluginContext* ctx, const cell_t* params)
        {
            ConMsg("%s", Str(ctx, params[1]));
            return 0;
        }

        cell_t N_ServerCommand(IPluginContext* ctx, const cell_t* params)
        {
            std::string msg;
            if (FormatTo(ctx, params, 1, msg) && g_pEngineServer)
                g_pEngineServer->ServerCommand((msg + "\n").c_str());
            return 0;
        }

        // The console line looks like a C++ plugin's SOURCE2MOD_LOG ("[Name]
        // text"); Source2Mod.log still gets it with the time and the file name.
        cell_t N_LogMessage(IPluginContext* ctx, const cell_t* params)
        {
            std::string msg;
            if (!FormatTo(ctx, params, 1, msg))
                return 0;
            ScriptPlugin* plugin = Plugin(ctx);
            ConMsg("[%s] %s\n", plugin ? plugin->GetName() : "?", msg.c_str());
            ::log::GetFileLogger()->info("[{}] {}", plugin ? plugin->FileName() : "?", msg);
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
                Invoke(fn, args);

                g_commandStack.pop_back();
            });

            commands::commandsManager.RegisterConCommand(plugin->Id(), name, std::move(handler), permission);
            return 0;
        }

        // ---- timers -------------------------------------------------------------

        // A timer is a scheduler timer plus its handle. Either side can end it:
        // closing the handle kills the timer, and when the scheduler drops the
        // timer -- fired once, stopped, gone at a map change, its plugin
        // unloading -- the handle is freed. The state is shared so whichever
        // goes second finds the other already gone.
        constexpr int kTimerDataHndlClose = 1 << 9;

        struct TimerState
        {
            Timer* timer = nullptr;
            cell_t handle = 0;
            PluginId owner = 0;
            cell_t data = 0;
            int flags = 0;
        };

        void CloseTimerData(TimerState& state)
        {
            if (state.flags & kTimerDataHndlClose)
            {
                state.flags &= ~kTimerDataHndlClose;
                if (state.data)
                    Handles().Free(state.data, state.owner);
            }
        }

        struct TimerObject : HandleObject
        {
            std::shared_ptr<TimerState> state;

            ~TimerObject() override
            {
                state->handle = 0;
                CloseTimerData(*state);
                if (Timer* timer = std::exchange(state->timer, nullptr))
                    scheduler::schedulerManager.KillTimer(timer);
            }
        };

        // Lives in the scheduler's callback, so it goes when the timer does.
        struct TimerGuard
        {
            explicit TimerGuard(std::shared_ptr<TimerState> s) : state(std::move(s)) {}
            TimerGuard(const TimerGuard&) = delete;
            TimerGuard& operator=(const TimerGuard&) = delete;

            std::shared_ptr<TimerState> state;

            ~TimerGuard()
            {
                state->timer = nullptr;
                CloseTimerData(*state);
                if (const cell_t handle = std::exchange(state->handle, 0))
                    Handles().Free(handle, state->owner, true);
            }
        };

        // A timer of either API: SourceMod's callback gets (Handle timer, any
        // data) and stops a repeating timer with Plugin_Stop; Source2Mod's
        // gets (any data) and repeats until it is killed.
        cell_t StartTimer(IPluginContext* ctx, float interval, cell_t funcId, cell_t data, int flags, bool sourcemod)
        {
            ScriptPlugin* plugin = Plugin(ctx);
            IPluginFunction* fn = ctx->GetFunctionById(funcId);
            if (!plugin || !fn)
                return ctx->ThrowNativeError("Invalid function id (%X)", funcId);

            auto state = std::make_shared<TimerState>();
            state->owner = plugin->Id();
            state->data = data;
            state->flags = flags;

            auto* object = new TimerObject();
            object->state = state;
            state->handle = Handles().Create(Types().timer, object, plugin->Id());
            if (!state->handle)
            {
                delete object;
                return ctx->ThrowNativeError("Handle limit reached");
            }

            // Action (Handle timer, any data); Plugin_Stop ends a repeating timer.
            const bool repeat = (state->flags & TIMER_FLAG_REPEAT) != 0;
            auto guard = std::make_shared<TimerGuard>(state);
            TimerCallback callback([guard, fn, repeat, sourcemod]()
            {
                const std::shared_ptr<TimerState>& s = guard->state;
                if (!s->handle)
                    return;

                sp::CallArgs args;
                if (sourcemod)
                    args.PushCell(s->handle);
                args.PushCell(s->data);

                cell_t res = kPluginContinue;
                Invoke(fn, args, &res);

                if (sourcemod && repeat && res == kPluginStop && s->handle)
                    Handles().Free(s->handle, s->owner, true);
            });

            // The script's timer flags are SourceMod's and match the scheduler's
            // bit for bit (TIMER_REPEAT, TIMER_FLAG_NO_MAPCHANGE).
            state->timer = scheduler::schedulerManager.AddTimer(plugin->Id(), interval, std::move(callback),
                                                                state->flags & (TIMER_FLAG_REPEAT | TIMER_FLAG_NO_MAPCHANGE));
            return state->handle;
        }

        // Source2ModScheduler.AddTimer(float interval, Source2ModTask callback, any data = 0, int flags = 0)
        cell_t N_S2MAddTimer(IPluginContext* ctx, const cell_t* params)
        {
            return StartTimer(ctx, sp_ctof(params[1]), params[2], params[3], params[4], false);
        }

        // Source2ModScheduler.KillTimer(Source2ModTimer timer): one that has ended is fine.
        cell_t N_S2MKillTimer(IPluginContext*, const cell_t* params)
        {
            if (!Handles().Read(params[1], Types().timer))
                return 0;
            PluginId owner = 0;
            Handles().OwnerOf(params[1], &owner);
            Handles().Free(params[1], owner, true);
            return 0;
        }

        // ---- handles --------------------------------------------------------------

        cell_t N_CloseHandle(IPluginContext* ctx, const cell_t* params)
        {
            if (params[1] == kInvalidHandle)
                return 0;

            ScriptPlugin* plugin = Plugin(ctx);
            const HandleError err = Handles().Free(params[1], plugin ? plugin->Id() : 0);
            if (err != HandleError::None)
                return ReportHandleError(ctx, params[1], err);
            return 0;
        }

        // CloneHandle(Handle hndl, Handle plugin=INVALID_HANDLE)
        cell_t N_CloneHandle(IPluginContext* ctx, const cell_t* params)
        {
            ScriptPlugin* target = ScriptPlugin::FromHandle(ctx, params[0] >= 2 ? params[2] : 0);
            if (!target)
                return 0;

            HandleError err;
            const cell_t clone = Handles().Clone(params[1], target->Id(), &err);
            if (!clone)
                return ReportHandleError(ctx, params[1], err);
            return clone;
        }

        cell_t N_GetMyHandle(IPluginContext* ctx, const cell_t*)
        {
            ScriptPlugin* plugin = Plugin(ctx);
            return plugin ? plugin->Handle() : 0;
        }

        cell_t N_GetPluginFilename(IPluginContext* ctx, const cell_t* params)
        {
            if (ScriptPlugin* plugin = ScriptPlugin::FromHandle(ctx, params[1]))
                ctx->StringToLocalUTF8(params[2], params[3], plugin->FileName().c_str(), nullptr);
            return 0;
        }

        // RequestFrame(RequestFrameCallback func, any data=0)
        cell_t N_RequestFrame(IPluginContext* ctx, const cell_t* params)
        {
            ScriptPlugin* plugin = Plugin(ctx);
            IPluginFunction* fn = ctx->GetFunctionById(params[1]);
            if (!plugin || !fn)
                return ctx->ThrowNativeError("Invalid function id (%X)", params[1]);

            const cell_t data = params[2];
            scheduler::schedulerManager.NextFrame(plugin->Id(), [fn, data]()
            {
                sp::CallArgs args;
                args.PushCell(data);
                Invoke(fn, args);
            });
            return 0;
        }

        // ---- events -------------------------------------------------------------

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

        // ---- Source2ModEvents / IGameEvent / IGameEventManager2 ---------------------

        // Events a script created: an Event handle the script owns, which
        // frees the event when it is closed without being fired.
        HandleType CreatedEventType()
        {
            static const HandleType t = Handles().CreateType("CreatedEvent", [](void* p)
            {
                auto* frame = static_cast<EventFrame*>(p);
                if (frame->event && shared::g_pGameEventManager)
                    shared::g_pGameEventManager->FreeEvent(frame->event);
                delete frame;
            }, Types().event, true);
            return t;
        }

        // HookGameEvent(name, handler(IGameEvent event, bool post, bool &dontBroadcast), bool post)
        cell_t N_S2MHookGameEvent(IPluginContext* ctx, const cell_t* params)
        {
            ScriptPlugin* plugin = Plugin(ctx);
            IPluginFunction* fn = ctx->GetFunctionById(params[2]);
            if (!plugin || !fn)
                return ctx->ThrowNativeError("Invalid function id (%X)", params[2]);

            // 0 for an event the game does not have, as the include says (the
            // manager would keep a listener for a name that never fires).
            if (shared::g_pGameEventManager && !shared::g_pGameEventManager->HasEventDescriptor(Str(ctx, params[1])))
                return 0;

            const PluginId owner = plugin->Id();
            GameEventHandler handler([fn, owner](IGameEvent* event, bool post, bool& dontBroadcast) -> Action
            {
                EventFrame frame{ event, &dontBroadcast };
                const cell_t handle = Handles().Create(Types().event, &frame, owner);
                cell_t broadcast = dontBroadcast ? 1 : 0;

                sp::CallArgs args;
                args.PushCell(handle);
                args.PushCell(post ? 1 : 0);
                args.PushCellByRef(&broadcast);
                cell_t res = 0;
                Invoke(fn, args, &res);

                dontBroadcast = broadcast != 0;
                Handles().Free(handle, owner, true);
                return static_cast<Action>(std::clamp<cell_t>(res, 0, 2));
            });
            return static_cast<cell_t>(events::eventManager.HookGameEvent(owner, Str(ctx, params[1]), std::move(handler), params[3] != 0));
        }

        cell_t N_S2MUnhookGameEvent(IPluginContext*, const cell_t* params)
        {
            return static_cast<ISource2ModEvents&>(events::eventManager).UnhookGameEvent(static_cast<Source2ModHookId>(params[1]));
        }

        cell_t N_GameEventGetPlayerSlot(IPluginContext* ctx, const cell_t* params)
        {
            IGameEvent* ev = EventOf(ctx, params[1]);
            return ev ? ev->GetPlayerSlot(Str(ctx, params[2])).Get() : -1;
        }

        cell_t N_GameEventGetPlayerController(IPluginContext* ctx, const cell_t* params)
        {
            IGameEvent* ev = EventOf(ctx, params[1]);
            return EntityCell(ev ? ev->GetPlayerController(Str(ctx, params[2])) : nullptr);
        }

        cell_t N_GameEventGetPlayerPawn(IPluginContext* ctx, const cell_t* params)
        {
            IGameEvent* ev = EventOf(ctx, params[1]);
            return EntityCell(ev ? ev->GetPlayerPawn(Str(ctx, params[2])) : nullptr);
        }

        cell_t N_GameEventSetPlayer(IPluginContext* ctx, const cell_t* params)
        {
            if (IGameEvent* ev = EventOf(ctx, params[1]))
                ev->SetPlayer(Str(ctx, params[2]), CPlayerSlot(params[3]));
            return 0;
        }

        // IGameEventManager2.CreateEvent(name, bool force = false)
        cell_t N_ManagerCreateEvent(IPluginContext* ctx, const cell_t* params)
        {
            ScriptPlugin* plugin = Plugin(ctx);
            IGameEvent* ev = shared::g_pGameEventManager ? shared::g_pGameEventManager->CreateEvent(Str(ctx, params[1]), params[2] != 0) : nullptr;
            if (!ev)
                return 0;
            auto* frame = new EventFrame{ ev, nullptr };
            const cell_t handle = Handles().Create(CreatedEventType(), frame, plugin ? plugin->Id() : 0);
            if (!handle)
            {
                shared::g_pGameEventManager->FreeEvent(ev);
                delete frame;
            }
            return handle;
        }

        // FireEvent(event, bool dontBroadcast = false): the engine frees it.
        cell_t N_ManagerFireEvent(IPluginContext* ctx, const cell_t* params)
        {
            auto* frame = static_cast<EventFrame*>(ReadHandle(ctx, params[1], CreatedEventType()));
            if (!frame)
                return 0;
            IGameEvent* ev = std::exchange(frame->event, nullptr);
            Handles().Free(params[1], 0, true);
            return shared::g_pGameEventManager->FireEvent(ev, params[2] != 0);
        }

        cell_t N_ManagerFreeEvent(IPluginContext* ctx, const cell_t* params)
        {
            if (ReadHandle(ctx, params[1], CreatedEventType()))
                Handles().Free(params[1], 0, true);
            return 0;
        }

        // CBasePlayerController.FireEventToClient(IGameEvent event): the event stays the script's.
        cell_t N_ControllerFireEventToClient(IPluginContext* ctx, const cell_t* params)
        {
            auto* controller = static_cast<CBasePlayerController*>(EntityArg(ctx, params[1]));
            if (!controller)
                return 0;
            if (IGameEvent* ev = EventOf(ctx, params[2]))
                controller->FireEventToClient(ev);
            return 0;
        }

        // Source2ModScheduler.NextFrame(Source2ModTask task, any data = 0)
        cell_t N_S2MNextFrame(IPluginContext* ctx, const cell_t* params)
        {
            return N_RequestFrame(ctx, params);
        }

        // ---- clients ------------------------------------------------------------

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

        // VFormat(char[] buffer, int maxlength, const char[] format, int varpos):
        // the arguments are those of the script function that called it,
        // from its parameter varpos on (SourceMod's sm_vformat).
        cell_t N_VFormat(IPluginContext* ctx, const cell_t* params)
        {
            auto* runtime = static_cast<sp::BaseRuntime*>(ctx->GetRuntime());
            const cell_t* local = runtime->GetLocalParams();
            const cell_t varpos = params[4];
            if (!local || varpos < 1 || varpos > local[0] + 1)
                return ctx->ThrowNativeError("Argument index is invalid: %d", varpos);

            // A parameter list FormatParams reads: the format, then the arguments.
            std::vector<cell_t> list;
            list.push_back(0);
            list.push_back(params[3]);
            for (cell_t i = varpos; i <= local[0]; i++)
                list.push_back(local[i]);
            list[0] = static_cast<cell_t>(list.size() - 1);

            std::string out(static_cast<size_t>(std::max(params[2], 1)), '\0');
            size_t written = 0;
            if (!FormatParams(ctx, list.data(), 1, out.data(), out.size(), &written))
                return 0;
            ctx->StringToLocalUTF8(params[1], params[2], out.c_str(), &written);
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
            { "Source2ModPlugins.ThrowError", N_ThrowError },
            { "Source2ModPlugins.SetFailState", N_SetFailState },
            { "Source2ModEngine.GetEngineTime", N_GetEngineTime },
            // console.inc / logging.inc
            { "Source2ModApi.ConPrintf", N_PrintToServer },
            { "Source2ModEngine.ServerCommand", N_ServerCommand },
            { "Source2ModApi.ConPrint", N_ConPrint },
            { "Source2ModApi.Log", N_LogMessage },
            { "Source2ModApi.LogError", N_LogError },
            // timers.inc / handles.inc
            { "Handle.~Handle", N_CloseHandle },
            { "Handle.Close", N_CloseHandle },
            { "Handle.Clone", N_CloneHandle },
            { "Source2ModPlugins.GetMyHandle", N_GetMyHandle },
            { "Source2ModPlugins.GetFilename", N_GetPluginFilename },
            // source2mod/scheduler.inc
            { "Source2ModScheduler.NextFrame", N_S2MNextFrame },
            { "Source2ModScheduler.AddTimer", N_S2MAddTimer },
            { "Source2ModScheduler.KillTimer", N_S2MKillTimer },
            // source2mod/events.inc
            { "Source2ModEvents.HookGameEvent", N_S2MHookGameEvent },
            { "Source2ModEvents.UnhookGameEvent", N_S2MUnhookGameEvent },
            { "IGameEvent.GetPlayerSlot", N_GameEventGetPlayerSlot },
            { "IGameEvent.GetPlayerController", N_GameEventGetPlayerController },
            { "IGameEvent.GetPlayerPawn", N_GameEventGetPlayerPawn },
            { "IGameEvent.SetPlayer", N_GameEventSetPlayer },
            { "IGameEventManager2.CreateEvent", N_ManagerCreateEvent },
            { "IGameEventManager2.FireEvent", N_ManagerFireEvent },
            { "IGameEventManager2.FreeEvent", N_ManagerFreeEvent },
            { "CBasePlayerController.FireEventToClient", N_ControllerFireEventToClient },
            // events.inc
            { "IGameEvent.GetBool", N_EventGetBool },
            { "IGameEvent.GetInt", N_EventGetInt },
            { "IGameEvent.GetFloat", N_EventGetFloat },
            { "IGameEvent.GetString", N_EventGetString },
            { "IGameEvent.SetBool", N_EventSetBool },
            { "IGameEvent.SetInt", N_EventSetInt },
            { "IGameEvent.SetFloat", N_EventSetFloat },
            { "IGameEvent.SetString", N_EventSetString },
            { "IGameEvent.GetName", N_EventGetName },
            // clients.inc / halflife.inc
            // string.inc
            { "Source2ModApi.Format", N_Format },
            { "Source2ModApi.FormatArgs", N_VFormat },
            { "Source2ModUtils.Length", N_strlen },
            { "Source2ModUtils.Copy", N_strcopy },
            { "Source2ModUtils.Compare", N_strcmp },
            { "Source2ModUtils.CompareN", N_strncmp },
            { "Source2ModUtils.IntToString", N_IntToString },
            { "Source2ModUtils.StringToInt", N_StringToInt },
            { "Source2ModUtils.FloatToString", N_FloatToString },
            { "Source2ModUtils.StringToFloat", N_StringToFloat },
            { nullptr, nullptr },
        };
    }

    Timer* ScriptTimer(IPluginContext* ctx, cell_t handle)
    {
        auto* object = static_cast<TimerObject*>(ReadHandle(ctx, handle, Types().timer));
        return object ? object->state->timer : nullptr;
    }

    IGameEvent* ScriptEvent(IPluginContext* ctx, cell_t handle)
    {
        return EventOf(ctx, handle);
    }

    cell_t NewScriptEvent(IGameEvent* event, PluginId owner)
    {
        auto* frame = new EventFrame{ event, nullptr };
        const cell_t handle = Handles().Create(CreatedEventType(), frame, owner);
        if (!handle)
        {
            shared::g_pGameEventManager->FreeEvent(event);
            delete frame;
        }
        return handle;
    }

    IGameEvent* TakeScriptEvent(IPluginContext* ctx, cell_t handle)
    {
        auto* frame = static_cast<EventFrame*>(ReadHandle(ctx, handle, CreatedEventType()));
        if (!frame)
            return nullptr;
        IGameEvent* ev = std::exchange(frame->event, nullptr);
        Handles().Free(handle, 0, true);
        return ev;
    }

    const sp_nativeinfo_t* GetCoreNatives()
    {
        static const std::vector<sp_nativeinfo_t> s_all = []
        {
            std::vector<sp_nativeinfo_t> all;
            for (const sp_nativeinfo_t* table : { g_natives, GetAdtNatives(), GetConVarNatives(), GetPluginSysNatives(), GetEntityNatives(), GetApiNatives(), GetNetSoundNatives(), GetGameHookNatives(), GetEventNatives(), GetGameConfigNatives(), GetRawHookNatives(), GetWorldNatives(), GetNetNatives(), GetUiNatives(), GetMathNatives(), GetFeatureNatives(), GetMapNatives(), GetGeoIPNatives(), GetCookieNatives(), GetTranslationNatives(), GetFileNatives(), GetUtilNatives(), GetClientNatives(), GetTargetingNatives() })
                for (const sp_nativeinfo_t* n = table; n->name; ++n)
                    all.push_back(*n);
            all.push_back({ nullptr, nullptr });
            return all;
        }();
        return s_all.data();
    }
}
