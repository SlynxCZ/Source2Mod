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
#include "crashinfo.h"
#include "handles.h"
#include "natives.h"

#include "core/convars.h"
#include "core/pluginmanager.h"
#include "core/scheduler.h"
#include "core/shared.h"
#include "utils/log.h"
#include "utils/paths.h"

#include <base-runtime.h>
#include <environment.h>
#include <objects.h>
#include <v2/runtime.h>
#include <sp_callable.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>

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
        // Load order, which is the order forwards run in.
        std::vector<ScriptPlugin*> g_scripts;

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
                FP_INFO("SourcePawn VM ready ({})", g_pEnv->IsJitAllowed() ? "JIT" : "interpreter");
            }
            return g_pEnv;
        }
    }

    bool Invoke(IPluginFunction* fn, const sp::CallArgs& args, cell_t* result)
    {
        if (!fn)
            return false;

        // The error has been reported to the debug listener by the time
        // Invoke() returns; the handler only keeps it from unwinding the
        // script whose native brought us here.
        ExceptionHandler eh(fn->GetParentRuntime()->GetDefaultContext());
        crashinfo::CallScope call(fn);
        return fn->Invoke(args, result);
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

    void RemoveAllForPlugin(PluginId id)
    {
        for (ScriptPlugin* script : g_scripts)
        {
            if (script->Id() == id && script->Handle())
                Handles().Free(script->Handle(), 0, true);
        }

        pluginsys::RemoveAllForPlugin(id);
        ForgetEventHooks(id);
        ForgetConVarHooks(id);
        ForgetGameHooks(id);
        ForgetFunctionHooks(id);
        ForgetEntityListeners(id);
        ForgetWorldHooks(id);
        ForgetUiHooks(id);
        ForgetNetSoundHooks(id);
        Handles().RemoveAllForPlugin(id);
    }

    std::shared_ptr<ScriptPlugin> Open(const char* path, std::string& error)
    {
        sp::Environment* env = Env();
        if (!env)
        {
            error = "the SourcePawn VM could not be created";
            return nullptr;
        }

        // The loader reports why through the environment's exception scope.
        SourcePawn::ExceptionHandler eh(env);
        std::unique_ptr<sp::BaseRuntime> runtime(env->LoadBinaryFromFile(path));
        if (!runtime)
        {
            error = eh.HasException() ? eh.Message() : "not a valid SourcePawn binary";
            return nullptr;
        }

        return std::make_shared<ScriptPlugin>(std::move(runtime), path);
    }

    // ---- ScriptPlugin -----------------------------------------------------------

    ScriptPlugin::ScriptPlugin(std::unique_ptr<sp::BaseRuntime> runtime, std::string path)
        : m_runtime(std::move(runtime)), m_path(std::move(path))
    {
        m_file = std::filesystem::path(m_path).filename().string();
        g_byContext[Context()] = this;
        g_scripts.push_back(this);
        crashinfo::Track(this);
        ReadMyInfo();
    }

    ScriptPlugin::~ScriptPlugin()
    {
        // RemoveAllForPlugin() took the handles, timers and hooks down already.
        crashinfo::Untrack(this);
        g_byContext.erase(Context());
        std::erase(g_scripts, this);
    }

    IPluginContext* ScriptPlugin::Context()
    {
        return m_runtime.get();
    }

    IPluginRuntime* ScriptPlugin::Runtime()
    {
        return m_runtime.get();
    }

    ScriptPlugin* ScriptPlugin::FromContext(IPluginContext* ctx)
    {
        auto it = g_byContext.find(ctx);
        return it != g_byContext.end() ? it->second : nullptr;
    }

    ScriptPlugin* ScriptPlugin::FromHandle(IPluginContext* ctx, cell_t handle)
    {
        if (handle == kInvalidHandle)
            return FromContext(ctx);
        return static_cast<ScriptPlugin*>(ReadHandle(ctx, handle, Types().plugin));
    }

    std::vector<ScriptPlugin*> ScriptPlugin::Running()
    {
        std::vector<ScriptPlugin*> out;
        for (ScriptPlugin* script : g_scripts)
            if (script->m_running)
                out.push_back(script);
        return out;
    }

    void ScriptPlugin::ReadMyInfo()
    {
        // public Plugin myinfo = { name, description, author, version, url }:
        // five strings, in that order.
        m_name = std::filesystem::path(m_path).stem().string();

        // A SourcePawn 2 binary keeps it as a public struct global in the
        // image (fields by name), not as a pubvar.
        if (m_runtime->AsV2())
        {
            sp::SmxImage* image = m_runtime->image();
            const sp::smx_pstruct_global* myinfo = image ? image->FindPstructGlobal("myinfo") : nullptr;
            if (!myinfo)
                return;
            auto field = [image, myinfo](const char* name) -> std::string
            {
                std::variant<std::string, cell_t> value;
                if (image->GetPstructValue(myinfo, name, &value) != SP_ERROR_NONE)
                    return {};
                const std::string* s = std::get_if<std::string>(&value);
                return s ? *s : std::string();
            };
            if (auto name = field("name"); !name.empty())
                m_name = name;
            m_description = field("description");
            m_author = field("author");
            m_version = field("version");
            m_url = field("url");
            return;
        }

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

    void ScriptPlugin::BindCoreNatives()
    {
        // Not InstallBuiltinNatives(): it binds the float operator natives for
        // opcode replacement, which the x64 JIT cannot compile, and a bound
        // native cannot be bound again. The core binds them all instead.
        for (const sp_nativeinfo_t* n = GetCoreNatives(); n->name; ++n)
        {
            uint32_t idx;
            if (m_runtime->FindNativeByName(n->name, &idx) == SP_ERROR_NONE)
                m_runtime->UpdateNativeBinding(idx, n->func, IsEphemeralMathNative(n->name) ? SP_NTVFLAG_EPHEMERAL : 0, nullptr);
        }

        // What other plugins registered so far.
        BindDynamicNatives();

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
                Invoke(m_runtime->GetFunctionByName(pub->name), sp::CallArgs());
        }
    }

    void ScriptPlugin::BindDynamicNatives()
    {
        const uint32_t natives = m_runtime->GetNativesNum();
        for (uint32_t i = 0; i < natives; i++)
        {
            const sp_native_t* native = m_runtime->GetNative(i);
            if (native && native->status != SP_NATIVE_BOUND)
                pluginsys::BindNative(m_runtime.get(), i, native->name);
        }
    }

    bool ScriptPlugin::CheckNatives(char* error, size_t maxlen, bool& missing)
    {
        // Natives a plugin made in its AskPluginLoad2 -- this one included --
        // are there now.
        BindDynamicNatives();

        std::string list;
        const uint32_t natives = m_runtime->GetNativesNum();
        for (uint32_t i = 0; i < natives; i++)
        {
            const sp_native_t* native = m_runtime->GetNative(i);
            if (!native || native->status == SP_NATIVE_BOUND || (native->flags & SP_NTVFLAG_OPTIONAL))
                continue;

            if (!list.empty())
                list += ", ";
            list += native->name;
        }

        missing = !list.empty();
        if (missing)
        {
            snprintf(error, maxlen, "Native(s) not found: %s", list.c_str());
            return false;
        }
        return true;
    }

    bool ScriptPlugin::Load(PluginId id, ISource2ModAPI* api, char* error, size_t maxlen, bool late)
    {
        m_id = id;
        m_handle = Handles().Create(Types().plugin, this, 0);

        // SourceMod's two passes in one: what exists now is bound before
        // AskPluginLoad2, which may create natives (CreateNative) and
        // libraries; only after it is a missing native an error.
        BindCoreNatives();

        // APLRes AskPluginLoad2(Handle myself, bool late, char[] error, int err_max)
        if (IPluginFunction* ask = m_runtime->GetFunctionByName("AskPluginLoad2"))
        {
            char reason[256]{};
            sp::CallArgs args;
            args.PushCell(m_handle);
            args.PushCell(late ? 1 : 0);
            args.PushString(reason, sizeof(reason), SM_PARAM_STRING_UTF8 | SM_PARAM_STRING_COPY);
            args.PushCell(sizeof(reason));

            cell_t res = 0;
            if (!Invoke(ask, args, &res))
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

        bool missing = false;
        if (!CheckNatives(error, maxlen, missing))
        {
            // Maybe a plugin that loads later has them.
            if (missing)
                pluginsys::NoteWaiting(m_path);
            return false;
        }

        m_running = true;

        if (IPluginFunction* start = m_runtime->GetFunctionByName("OnPluginStart"))
        {
            if (!Invoke(start, sp::CallArgs()))
            {
                m_running = false;
                snprintf(error, maxlen, "Error detected in plugin startup (see the error above)");
                return false;
            }
        }

        pluginManager.AddListener(this, this);
        pluginsys::OnPluginLoaded(m_id);

        // A plugin loaded into a running server gets what it missed.
        if (late)
        {
            CallForward("Source2Mod_OnAllSource2ModPluginsLoaded");
            if (shared::getGlobalVars())
            {
                ExecAutoConfig();
                CallForward("OnAutoConfigsBuffered");
                ConfigsExecutedSoon();
            }
        }

        return true;
    }

    bool ScriptPlugin::Unload(char* error, size_t maxlen)
    {
        if (m_running)
            CallForward("OnPluginEnd");
        m_running = false;
        return true;
    }

    void ScriptPlugin::CallForward(const char* name)
    {
        if (IPluginFunction* fn = m_runtime->GetFunctionByName(name))
            Invoke(fn, sp::CallArgs());
    }

    void ScriptPlugin::CallForwardClient(const char* name, int client)
    {
        if (IPluginFunction* fn = m_runtime->GetFunctionByName(name))
        {
            sp::CallArgs args;
            args.PushCell(client);
            Invoke(fn, args);
        }
    }

    void ScriptPlugin::CallForwardString(const char* name, const char* value)
    {
        if (IPluginFunction* fn = m_runtime->GetFunctionByName(name))
        {
            sp::CallArgs args;
            args.PushString(value);
            Invoke(fn, args);
        }
    }

    void ScriptPlugin::OnAllSource2ModPluginsLoaded()
    {
        CallForward("Source2Mod_OnAllSource2ModPluginsLoaded");
    }

    void ScriptPlugin::OnLevelInit(const char* mapName, const char* mapEntities, const char* oldLevel, const char* landmarkName, bool loadGame, bool background)
    {
        if (IPluginFunction* fn = m_runtime->GetFunctionByName("Source2Mod_OnLevelInit"))
        {
            sp::CallArgs args;
            args.PushString(mapName ? mapName : "");
            args.PushString(mapEntities ? mapEntities : "");
            args.PushString(oldLevel ? oldLevel : "");
            args.PushString(landmarkName ? landmarkName : "");
            args.PushCell(loadGame ? 1 : 0);
            args.PushCell(background ? 1 : 0);
            Invoke(fn, args);
        }
        ExecAutoConfig();
        CallForward("OnAutoConfigsBuffered");
        ConfigsExecutedSoon();
    }

    void ScriptPlugin::OnLevelShutdown()
    {
        CallForward("Source2Mod_OnLevelShutdown");
    }

    void ScriptPlugin::OnClientAuthorized(CPlayerSlot slot, uint64 steamId)
    {
        if (IPluginFunction* fn = m_runtime->GetFunctionByName("Source2Mod_OnClientAuthorized"))
        {
            const std::string id = std::to_string(steamId);
            sp::CallArgs args;
            args.PushCell(slot.Get());
            args.PushString(id.c_str());
            Invoke(fn, args);
        }
    }

    void ScriptPlugin::OnClientAuthorizeFailed(CPlayerSlot slot, uint64 steamId)
    {
        if (IPluginFunction* fn = m_runtime->GetFunctionByName("Source2Mod_OnClientAuthorizeFailed"))
        {
            const std::string id = std::to_string(steamId);
            sp::CallArgs args;
            args.PushCell(slot.Get());
            args.PushString(id.c_str());
            Invoke(fn, args);
        }
    }

    void ScriptPlugin::OnPermissionsChanged(uint64 steamId)
    {
        CallForwardString("Source2Mod_OnPermissionsChanged", std::to_string(steamId).c_str());
    }

    void ScriptPlugin::CallForwardPlugin(const char* name, PluginId id, const char* pluginName)
    {
        if (IPluginFunction* fn = m_runtime->GetFunctionByName(name))
        {
            sp::CallArgs args;
            args.PushCell(id);
            if (pluginName)
                args.PushString(pluginName);
            Invoke(fn, args);
        }
    }

    void ScriptPlugin::OnPluginLoad(PluginId id, const char* name)
    {
        CallForwardPlugin("Source2Mod_OnPluginLoad", id, name ? name : "");
    }

    void ScriptPlugin::OnPluginUnload(PluginId id, const char* name)
    {
        CallForwardPlugin("Source2Mod_OnPluginUnload", id, name ? name : "");
    }

    void ScriptPlugin::OnMetamodPluginLoad(PluginId id)
    {
        CallForwardPlugin("Source2Mod_OnMetamodPluginLoad", id, nullptr);
    }

    void ScriptPlugin::OnMetamodPluginUnload(PluginId id)
    {
        CallForwardPlugin("Source2Mod_OnMetamodPluginUnload", id, nullptr);
    }

    void ScriptPlugin::OnAllMetamodPluginsLoaded()
    {
        CallForward("Source2Mod_OnAllMetamodPluginsLoaded");
    }

    // ---- AutoExecConfig ---------------------------------------------------------

    void ScriptPlugin::NoteConVar(CreatedConVar cvar)
    {
        for (const CreatedConVar& c : m_createdConVars)
            if (c.name == cvar.name)
                return;
        m_createdConVars.push_back(std::move(cvar));
    }

    void ScriptPlugin::RequestAutoExecConfig(bool autoCreate, std::string name, std::string folder)
    {
        m_autoExec = true;
        m_autoCreate = autoCreate;
        m_autoName = std::move(name);
        m_autoFolder = std::move(folder);
    }

    void ScriptPlugin::ExecAutoConfig()
    {
        if (!m_autoExec)
            return;

        // cfg/<folder>/<name>.cfg; "plugin.<file>" without a name, as SourceMod does.
        const std::string folder = m_autoFolder.empty() ? "source2mod" : m_autoFolder;
        const std::string name = m_autoName.empty() ? "plugin." + std::filesystem::path(m_path).stem().string() : m_autoName;
        const std::filesystem::path dir = std::filesystem::path(paths::GameDirectory()) / "cfg" / folder;
        const std::filesystem::path file = dir / (name + ".cfg");

        std::error_code ec;
        if (!std::filesystem::exists(file, ec) && m_autoCreate)
        {
            std::filesystem::create_directories(dir, ec);
            std::ofstream out(file);
            if (!out)
            {
                FP_ERROR("[{}] AutoExecConfig: cannot write {}", m_file, file.string());
                return;
            }

            out << "// This file was auto-generated by Source2Mod\n";
            out << "// ConVars for plugin \"" << m_file << "\"\n\n";
            for (const CreatedConVar& c : m_createdConVars)
            {
                out << "\n";
                std::string desc = c.description;
                size_t pos = 0;
                while (pos <= desc.size())
                {
                    const size_t nl = desc.find('\n', pos);
                    out << "// " << desc.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos) << "\n";
                    if (nl == std::string::npos)
                        break;
                    pos = nl + 1;
                }
                out << "// -\n";
                out << "// Default: \"" << c.defaultValue << "\"\n";
                char bound[64];
                if (c.hasMin)
                {
                    snprintf(bound, sizeof(bound), "%f", c.min);
                    out << "// Minimum: \"" << bound << "\"\n";
                }
                if (c.hasMax)
                {
                    snprintf(bound, sizeof(bound), "%f", c.max);
                    out << "// Maximum: \"" << bound << "\"\n";
                }
                out << c.name << " \"" << c.defaultValue << "\"\n\n";
            }
            FP_INFO("[{}] AutoExecConfig: wrote {}", m_file, file.string());
        }

        ApplyConfigFile(file);
    }

    void ScriptPlugin::ApplyConfigFile(const std::filesystem::path& file)
    {
        // Not "exec": with a workshop addon mounted CS2 runs a cfg as a
        // workshop config and refuses every ConVar that is not the game's own
        // ("DISALLOWED WORKSHOP CONVAR"). ConVar lines are set here instead;
        // anything else goes to the command buffer as exec would have put it.
        std::ifstream in(file);
        if (!in)
            return;

        std::string line;
        while (std::getline(in, line))
        {
            if (const size_t comment = line.find("//"); comment != std::string::npos)
                line.erase(comment);
            while (!line.empty() && isspace(static_cast<unsigned char>(line.back())))
                line.pop_back();
            size_t start = 0;
            while (start < line.size() && isspace(static_cast<unsigned char>(line[start])))
                start++;
            line.erase(0, start);
            if (line.empty())
                continue;

            const size_t split = line.find_first_of(" \t");
            const std::string cvar = line.substr(0, split);
            std::string value = split == std::string::npos ? "" : line.substr(line.find_first_not_of(" \t", split));
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                value = value.substr(1, value.size() - 2);

            const uint16 index = convars::convarsManager.GetConvarAccessIndexByName(cvar.c_str());
            if (index != SOURCE2MOD_INVALID_CONVAR_INDEX)
                static_cast<ISource2ModConVars&>(convars::convarsManager).SetString(index, value.c_str());
            else if (g_pEngineServer)
                g_pEngineServer->ServerCommand((line + "\n").c_str());
        }
    }

    void ScriptPlugin::ConfigsExecutedSoon()
    {
        // server.cfg and what a config handed to the command buffer run at
        // the start of a frame; two frames on, they have.
        scheduler::schedulerManager.NextFrame(m_id, [this]()
        {
            scheduler::schedulerManager.NextFrame(m_id, [this]()
            {
                if (m_running)
                    CallForward("OnConfigsExecuted");
            });
        });
    }
}
