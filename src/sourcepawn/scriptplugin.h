/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * SourcePawn script plugins: the .smx loader and the plugin object the plugin
 * manager holds for a script.
 *
 * Project: Source2Mod
 */
#pragma once

#include <source2mod/ISource2ModPlugin.h>
#include <source2mod/ISource2ModScheduler.h>

#include <sp_vm_api.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace sp
{
    class PluginRuntime;
}

class IGameEvent;

/**
 * SourcePawn plugins. A .smx in the plugins folder is either a native plugin
 * (a shared library: ELF / PE) or a SourcePawn binary (the SMX magic); the
 * plugin manager asks IsScriptFile() and, for a script, loads it through Open().
 * What comes back is an ordinary ISource2ModPlugin to the manager -- same ids,
 * same listener fan-out, same RemoveAllForPlugin() cleanup -- whose Load()
 * binds the natives, runs AskPluginLoad2 and OnPluginStart, and whose listener
 * turns the engine callbacks into SourceMod's forwards.
 */
namespace sourcepawn
{
    /// The file starts with the SMX magic.
    bool IsScriptFile(const char* path);

    /// The VM, created on the first script and torn down with the core.
    void Shutdown();

    class ScriptPlugin : public ISource2ModPlugin, public ISource2ModListener
    {
    public:
        ScriptPlugin(std::unique_ptr<sp::PluginRuntime> runtime, std::string path);
        ~ScriptPlugin() override;

        // ISource2ModPlugin
        bool Load(PluginId id, ISource2ModAPI* api, char* error, size_t maxlen, bool late) override;
        bool Unload(char* error, size_t maxlen) override;
        const char* GetAuthor() override { return m_author.c_str(); }
        const char* GetName() override { return m_name.c_str(); }
        const char* GetDescription() override { return m_description.c_str(); }
        const char* GetVersion() override { return m_version.c_str(); }
        const char* GetKHookCommit() override { return ""; }
        int GetRawHookCount() override { return 0; }

        // ISource2ModListener -> SourceMod forwards
        void OnAllSource2ModPluginsLoaded() override;
        void OnLevelInit(const char* mapName, const char* mapEntities, const char* oldLevel, const char* landmarkName, bool loadGame, bool background) override;
        void OnLevelShutdown() override;
        void OnGameFrame(bool simulating, bool firstTick, bool lastTick) override;
        void OnClientPutInServer(CPlayerSlot slot, const char* name, int type, uint64 xuid) override;
        void OnClientDisconnect(CPlayerSlot slot, ENetworkDisconnectionReason reason, const char* name, uint64 xuid, const char* networkId) override;
        void OnClientAuthorized(CPlayerSlot slot, uint64 steamId) override;

        PluginId Id() const { return m_id; }
        const std::string& Path() const { return m_path; }
        const std::string& FileName() const { return m_file; }
        SourcePawn::IPluginContext* Context();

        /// Runs a public by name with no arguments, if the plugin has it.
        void CallForward(const char* name);
        void CallForwardClient(const char* name, int client);

        // Handles. A Handle is a plugin-local number here; timers are the
        // only kind that outlives a call, event handles live for one callback.
        cell_t AddTimer(float interval, SourcePawn::IPluginFunction* fn, cell_t data, int flags);
        bool KillTimer(cell_t handle);

        /// The script a native was called from.
        static ScriptPlugin* FromContext(SourcePawn::IPluginContext* ctx);

    private:
        bool BindNatives(char* error, size_t maxlen);
        void ReadMyInfo();

        std::unique_ptr<sp::PluginRuntime> m_runtime;
        std::string m_path;
        std::string m_file;
        PluginId m_id = 0;

        std::string m_name, m_description, m_author, m_version, m_url;

        SourcePawn::IPluginFunction* m_onGameFrame = nullptr;

        cell_t m_nextHandle = 1;
        // Shared with the timer callbacks, which drop their handle when the
        // scheduler drops the timer -- possibly after this object is gone.
        std::shared_ptr<std::unordered_map<cell_t, Timer*>> m_timers = std::make_shared<std::unordered_map<cell_t, Timer*>>();
    };

    /// Loads a .smx script; nullptr and the reason when it is not one the VM takes.
    std::shared_ptr<ScriptPlugin> Open(const char* path, std::string& error);
}
