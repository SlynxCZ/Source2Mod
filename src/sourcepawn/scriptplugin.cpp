/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * SourcePawn script plugins: the VM, the .smx loader and the forwards.
 * SourceMod's plugin system (AlliedModders LLC) is the model for the load
 * sequence -- optional natives, AskPluginLoad2, OnPluginStart -- and for the
 * forwards a script expects.
 *
 * Project: Source2Mod
 */
#include "scriptplugin.h"
#include "natives.h"

#include "core/pluginmanager.h"
#include "core/scheduler.h"
#include "core/shared.h"
#include "utils/log.h"

#include <environment.h>
#include <plugin-runtime.h>
#include <sp_callable.h>

#include <cstdio>
#include <cstring>
#include <filesystem>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        // "FFPS": the first four bytes of every SMX file.
        constexpr uint32_t kSmxMagic = 0x53504646;

        // SourceMod's order and wording, so a script error reads the same as
        // it did there: the message, the plugin to blame, the call stack.
        class DebugListener : public IDebugListener
        {
        public:
            void OnDebugSpew(const char* msg, ...) override {}

            void ReportError(const IErrorReport& report, IFrameIterator& iter) override
            {
                ScriptPlugin* plugin = report.Context() ? ScriptPlugin::FromContext(report.Context()) : nullptr;

                FP_ERROR("Exception reported: {}", report.Message());
                FP_ERROR("Blaming: {}", plugin ? plugin->FileName() : std::string("<unknown>"));
                FP_ERROR("Call stack trace:");

                int frame = 0;
                for (; !iter.Done(); iter.Next())
                {
                    if (iter.IsInternalFrame())
                        continue;

                    if (iter.IsNativeFrame())
                        FP_ERROR("  [{}] {}", frame++, iter.FunctionName() ? iter.FunctionName() : "<unknown native>");
                    else if (iter.IsScriptedFrame())
                        FP_ERROR("  [{}] Line {}, {}::{}", frame++, iter.LineNumber(),
                                 iter.FilePath() ? iter.FilePath() : "<unknown>",
                                 iter.FunctionName() ? iter.FunctionName() : "<unknown>");
                }
            }
        };

        DebugListener g_DebugListener;
        sp::Environment* g_pEnv = nullptr;

        std::unordered_map<IPluginContext*, ScriptPlugin*> g_byContext;

        sp::Environment* Env()
        {
            if (!g_pEnv)
            {
                g_pEnv = sp::Environment::New();
                if (!g_pEnv)
                    return nullptr;

                g_pEnv->SetDebugger(&g_DebugListener);
                // A script stuck in a loop takes the server with it; ten
                // seconds without returning is an error, as in SourceMod.
                g_pEnv->InstallWatchdogTimer(10000);
                FP_INFO("SourcePawn VM ready ({})", g_pEnv->IsJitAvailable() ? "JIT" : "interpreter");
            }
            return g_pEnv;
        }

        bool Call(IPluginFunction* fn, const sp::CallArgs& args, cell_t* result = nullptr)
        {
            if (!fn)
                return false;
            // An error inside is reported (with its stack) to the debug
            // listener; the false here only tells the caller it happened.
            return fn->Invoke(args, result);
        }

        // SteamID64 -> "STEAM_1:Y:Z", what SourceMod's OnClientAuthorized hands over.
        std::string Steam2(uint64 steamId)
        {
            const uint32_t account = static_cast<uint32_t>(steamId & 0xFFFFFFFFu);
            char buf[64];
            snprintf(buf, sizeof(buf), "STEAM_1:%u:%u", account & 1u, account >> 1);
            return buf;
        }
    }

    bool IsScriptFile(const char* path)
    {
        FILE* fp = fopen(path, "rb");
        if (!fp)
            return false;

        uint32_t magic = 0;
        const bool read = fread(&magic, sizeof(magic), 1, fp) == 1;
        fclose(fp);
        return read && magic == kSmxMagic;
    }

    void Shutdown()
    {
        if (!g_pEnv)
            return;

        g_pEnv->SetDebugger(nullptr);
        g_pEnv->Shutdown();
        delete g_pEnv;
        g_pEnv = nullptr;
    }

    std::shared_ptr<ScriptPlugin> Open(const char* path, std::string& error)
    {
        sp::Environment* env = Env();
        if (!env)
        {
            error = "the SourcePawn VM could not be created";
            return nullptr;
        }

        char err[256]{};
        std::unique_ptr<sp::PluginRuntime> runtime(env->LoadBinaryFromFile(path, err, sizeof(err)));
        if (!runtime)
        {
            error = err[0] ? err : "not a valid SourcePawn binary";
            return nullptr;
        }

        return std::make_shared<ScriptPlugin>(std::move(runtime), path);
    }

    // ---- ScriptPlugin -----------------------------------------------------------

    ScriptPlugin::ScriptPlugin(std::unique_ptr<sp::PluginRuntime> runtime, std::string path)
        : m_runtime(std::move(runtime)), m_path(std::move(path))
    {
        m_file = std::filesystem::path(m_path).filename().string();
        g_byContext[Context()] = this;
        ReadMyInfo();
        m_onGameFrame = m_runtime->GetFunctionByName("OnGameFrame");
    }

    ScriptPlugin::~ScriptPlugin()
    {
        // RemoveAllForPlugin() took the timers down already.
        g_byContext.erase(Context());
    }

    IPluginContext* ScriptPlugin::Context()
    {
        return m_runtime.get();
    }

    ScriptPlugin* ScriptPlugin::FromContext(IPluginContext* ctx)
    {
        auto it = g_byContext.find(ctx);
        return it != g_byContext.end() ? it->second : nullptr;
    }

    void ScriptPlugin::ReadMyInfo()
    {
        // public Plugin myinfo = { name, description, author, version, url }:
        // five string addresses, in that order.
        m_name = std::filesystem::path(m_path).stem().string();

        uint32_t idx;
        if (m_runtime->FindPubvarByName("myinfo", &idx) != SP_ERROR_NONE)
            return;

        cell_t local;
        cell_t* info;
        if (m_runtime->GetPubvarAddrs(idx, &local, &info) != SP_ERROR_NONE)
            return;

        auto str = [this](cell_t addr) -> std::string
        {
            char* s = nullptr;
            if (m_runtime->LocalToString(addr, &s) != SP_ERROR_NONE || !s)
                return {};
            return s;
        };

        if (auto name = str(info[0]); !name.empty())
            m_name = name;
        m_description = str(info[1]);
        m_author = str(info[2]);
        m_version = str(info[3]);
        m_url = str(info[4]);
    }

    bool ScriptPlugin::BindNatives(char* error, size_t maxlen)
    {
        m_runtime->InstallBuiltinNatives();

        for (const sp_nativeinfo_t* n = GetCoreNatives(); n->name; ++n)
        {
            uint32_t idx;
            if (m_runtime->FindNativeByName(n->name, &idx) == SP_ERROR_NONE)
                m_runtime->UpdateNativeBinding(idx, n->func, 0, nullptr);
        }

        // MaxClients is a public the host fills in.
        uint32_t idx;
        if (m_runtime->FindPubvarByName("MaxClients", &idx) == SP_ERROR_NONE)
        {
            cell_t local;
            cell_t* addr;
            if (m_runtime->GetPubvarAddrs(idx, &local, &addr) == SP_ERROR_NONE)
                *addr = kMaxClients;
        }

        // The includes mark what may be missing: __ext_<name>_SetNTVOptional
        // and __pl_<name>_SetNTVOptional call MarkNativeAsOptional.
        const uint32_t publics = m_runtime->GetPublicsNum();
        for (uint32_t i = 0; i < publics; i++)
        {
            sp_public_t* pub = nullptr;
            if (m_runtime->GetPublicByIndex(i, &pub) != SP_ERROR_NONE || !pub || !pub->name)
                continue;

            const std::string name = pub->name;
            const bool ext = name.rfind("__ext_", 0) == 0 || name.rfind("__pl_", 0) == 0;
            if (ext && name.size() > 15 && name.compare(name.size() - 15, 15, "_SetNTVOptional") == 0)
                Call(m_runtime->GetFunctionByName(pub->name), sp::CallArgs());
        }

        std::string missing;
        const uint32_t natives = m_runtime->GetNativesNum();
        for (uint32_t i = 0; i < natives; i++)
        {
            const sp_native_t* native = m_runtime->GetNative(i);
            if (!native || native->status == SP_NATIVE_BOUND || (native->flags & SP_NTVFLAG_OPTIONAL))
                continue;

            if (!missing.empty())
                missing += ", ";
            missing += native->name;
        }

        if (!missing.empty())
        {
            snprintf(error, maxlen, "Native(s) not found: %s", missing.c_str());
            return false;
        }

        return true;
    }

    bool ScriptPlugin::Load(PluginId id, ISource2ModAPI* api, char* error, size_t maxlen, bool late)
    {
        m_id = id;

        if (!BindNatives(error, maxlen))
            return false;

        // APLRes AskPluginLoad2(Handle myself, bool late, char[] error, int err_max)
        if (IPluginFunction* ask = m_runtime->GetFunctionByName("AskPluginLoad2"))
        {
            char reason[256]{};
            sp::CallArgs args;
            args.PushCell(0);
            args.PushCell(late ? 1 : 0);
            args.PushString(reason, sizeof(reason), SM_PARAM_STRING_UTF8 | SM_PARAM_STRING_COPY);
            args.PushCell(sizeof(reason));

            cell_t res = 0;
            if (!Call(ask, args, &res))
            {
                snprintf(error, maxlen, "AskPluginLoad2 failed");
                return false;
            }
            // APLRes_Success = 0, APLRes_Failure, APLRes_SilentFailure
            if (res == 1)
            {
                snprintf(error, maxlen, "%s", reason[0] ? reason : "AskPluginLoad2 refused the load");
                return false;
            }
            if (res == 2)
            {
                snprintf(error, maxlen, "%s", reason[0] ? reason : "(silent)");
                return false;
            }
        }

        if (IPluginFunction* start = m_runtime->GetFunctionByName("OnPluginStart"))
        {
            if (!Call(start, sp::CallArgs()))
            {
                snprintf(error, maxlen, "Error detected in plugin startup (see the error above)");
                return false;
            }
        }

        pluginManager.AddListener(this, this);

        // A plugin loaded into a running server gets what it missed, as in SourceMod.
        if (late)
        {
            CallForward("OnAllPluginsLoaded");
            if (shared::getGlobalVars())
                CallForward("OnMapStart");
        }

        return true;
    }

    bool ScriptPlugin::Unload(char* error, size_t maxlen)
    {
        CallForward("OnPluginEnd");
        return true;
    }

    void ScriptPlugin::CallForward(const char* name)
    {
        if (IPluginFunction* fn = m_runtime->GetFunctionByName(name))
            Call(fn, sp::CallArgs());
    }

    void ScriptPlugin::CallForwardClient(const char* name, int client)
    {
        if (IPluginFunction* fn = m_runtime->GetFunctionByName(name))
        {
            sp::CallArgs args;
            args.PushCell(client);
            Call(fn, args);
        }
    }

    void ScriptPlugin::OnAllSource2ModPluginsLoaded()
    {
        CallForward("OnAllPluginsLoaded");
    }

    void ScriptPlugin::OnLevelInit(const char*, const char*, const char*, const char*, bool, bool)
    {
        CallForward("OnMapStart");
    }

    void ScriptPlugin::OnLevelShutdown()
    {
        CallForward("OnMapEnd");
    }

    void ScriptPlugin::OnGameFrame(bool, bool, bool)
    {
        if (m_onGameFrame)
            Call(m_onGameFrame, sp::CallArgs());
    }

    void ScriptPlugin::OnClientPutInServer(CPlayerSlot slot, const char*, int, uint64)
    {
        CallForwardClient("OnClientPutInServer", slot.Get() + 1);
    }

    void ScriptPlugin::OnClientDisconnect(CPlayerSlot slot, ENetworkDisconnectionReason, const char*, uint64, const char*)
    {
        CallForwardClient("OnClientDisconnect", slot.Get() + 1);
        CallForwardClient("OnClientDisconnect_Post", slot.Get() + 1);
    }

    void ScriptPlugin::OnClientAuthorized(CPlayerSlot slot, uint64 steamId)
    {
        if (IPluginFunction* fn = m_runtime->GetFunctionByName("OnClientAuthorized"))
        {
            const std::string auth = Steam2(steamId);
            sp::CallArgs args;
            args.PushCell(slot.Get() + 1);
            args.PushString(auth.c_str());
            Call(fn, args);
        }
    }

    // ---- timers ---------------------------------------------------------------

    namespace
    {
        // Lives in the timer's callback: when the scheduler drops the timer --
        // fired once, killed, gone at a map change, its plugin unloading --
        // the handle goes with it, so a stale handle never reaches KillTimer.
        struct TimerHandleGuard
        {
            cell_t handle;
            std::weak_ptr<std::unordered_map<cell_t, Timer*>> timers;
            ~TimerHandleGuard()
            {
                if (auto list = timers.lock())
                    list->erase(handle);
            }
        };
    }

    cell_t ScriptPlugin::AddTimer(float interval, IPluginFunction* fn, cell_t data, int flags)
    {
        const cell_t handle = m_nextHandle++;
        auto guard = std::make_shared<TimerHandleGuard>(TimerHandleGuard{ handle, m_timers });

        // Action (Handle timer, any data); Plugin_Stop ends a repeating timer.
        const bool repeat = (flags & TIMER_FLAG_REPEAT) != 0;
        TimerCallback callback([this, guard, fn, data, handle, repeat]()
        {
            sp::CallArgs args;
            args.PushCell(handle);
            args.PushCell(data);

            cell_t res = 0;
            Call(fn, args, &res);

            if (repeat && res == kPluginStop)
                KillTimer(handle);
        });

        // The script's timer flags are SourceMod's and match the scheduler's
        // bit for bit (TIMER_REPEAT, TIMER_FLAG_NO_MAPCHANGE).
        Timer* timer = scheduler::schedulerManager.AddTimer(m_id, interval, std::move(callback),
                                                            flags & (TIMER_FLAG_REPEAT | TIMER_FLAG_NO_MAPCHANGE));
        (*m_timers)[handle] = timer;
        return handle;
    }

    bool ScriptPlugin::KillTimer(cell_t handle)
    {
        auto it = m_timers->find(handle);
        if (it == m_timers->end())
            return false;

        Timer* timer = it->second;
        m_timers->erase(it);
        scheduler::schedulerManager.KillTimer(timer);
        return true;
    }
}
