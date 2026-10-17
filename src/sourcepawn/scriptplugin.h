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
 * SourcePawn script plugins: the .smx loader and the plugin object the plugin
 * manager holds for a script.
 */
#pragma once

#include <source2mod/ISource2ModPlugin.h>
#include <source2mod/ISource2ModScheduler.h>

#include <sp_vm_api.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace sp
{
    class PluginRuntime;
    struct CallArgs;
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

    /// What a plugin leaves behind in the SourcePawn layer -- its handles,
    /// the natives and libraries it registered, its functions in other
    /// plugins' forwards -- goes with it. Called by the plugin manager for
    /// every plugin, native ones included.
    void RemoveAllForPlugin(PluginId id);

    /**
     * Calls into a script from the host: a timer, a hook, a forward. An error
     * inside is reported (with its stack) and stops there instead of running
     * on into whatever script code called the native that led here.
     */
    bool Invoke(SourcePawn::IPluginFunction* fn, const sp::CallArgs& args, cell_t* result = nullptr);

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
        void OnClientAuthorized(CPlayerSlot slot, uint64 steamId) override;
        void OnClientAuthorizeFailed(CPlayerSlot slot, uint64 steamId) override;
        void OnPermissionsChanged(uint64 steamId) override;
        void OnPluginLoad(PluginId id, const char* name) override;
        void OnPluginUnload(PluginId id, const char* name) override;
        void OnMetamodPluginLoad(PluginId id) override;
        void OnMetamodPluginUnload(PluginId id) override;
        void OnAllMetamodPluginsLoaded() override;

        PluginId Id() const { return m_id; }
        const std::string& Path() const { return m_path; }
        const std::string& FileName() const { return m_file; }
        SourcePawn::IPluginContext* Context();
        SourcePawn::IPluginRuntime* Runtime();

        /// Between OnPluginStart and OnPluginEnd: what forwards and fake
        /// natives may call into.
        bool IsRunning() const { return m_running; }

        /// The Plugin handle scripts see for this one (GetMyHandle, AskPluginLoad2).
        cell_t Handle() const { return m_handle; }

        /// Runs a public by name with no arguments, if the plugin has it.
        void CallForward(const char* name);
        void CallForwardClient(const char* name, int client);
        void CallForwardString(const char* name, const char* value);
        /// A plugin id, then its name when there is one.
        void CallForwardPlugin(const char* name, PluginId id, const char* pluginName);

        /// Binds natives registered after this script loaded (CreateNative of
        /// a plugin loaded later, a native plugin's AddNatives).
        void BindDynamicNatives();

        // AutoExecConfig: the ConVars this plugin made, what to write and run.
        struct CreatedConVar
        {
            std::string name, defaultValue, description;
            bool hasMin, hasMax;
            float min, max;
        };
        void NoteConVar(CreatedConVar cvar);
        void RequestAutoExecConfig(bool autoCreate, std::string name, std::string folder);

        /// The script a native was called from.
        static ScriptPlugin* FromContext(SourcePawn::IPluginContext* ctx);
        /// The script behind a Plugin handle; 0 is the caller.
        static ScriptPlugin* FromHandle(SourcePawn::IPluginContext* ctx, cell_t handle);
        /// Every script that is running, in load order.
        static std::vector<ScriptPlugin*> Running();

    private:
        void BindCoreNatives();
        bool CheckNatives(char* error, size_t maxlen, bool& onlyPluginNatives);
        void ReadMyInfo();
        void ExecAutoConfig();
        void ApplyConfigFile(const std::filesystem::path& file);
        void ConfigsExecutedSoon();

        std::unique_ptr<sp::PluginRuntime> m_runtime;
        std::string m_path;
        std::string m_file;
        PluginId m_id = 0;
        cell_t m_handle = 0;
        bool m_running = false;

        std::string m_name, m_description, m_author, m_version, m_url;


        bool m_autoExec = false;
        bool m_autoCreate = true;
        std::string m_autoName, m_autoFolder;
        std::vector<CreatedConVar> m_createdConVars;
    };

    /// Loads a .smx script; nullptr and the reason when it is not one the VM takes.
    std::shared_ptr<ScriptPlugin> Open(const char* path, std::string& error);
}
