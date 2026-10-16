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
#pragma once
#include "hookid.h"

#include <entityhandle.h>
#include <source2mod/ISource2ModScripts.h>
#include <source2mod/utils/plat.h>

#include <string>
#include <unordered_map>
#include <vector>

class CBaseEntity;
class CEntityInstance;

namespace scripts
{
    /// CCSScript_EntityScript::RunScript(component, path, source): compiles and
    /// evaluates the source as an ES module in the component's context. The
    /// path only names the module ("file:///<path>.js") for errors and imports;
    /// the game appends "_c" and maps ".vjs_c" to ".js".
    using RunScript_t = void (FASTCALL*)(void* pComponent, const char* pszPath, const char* pszSource);

    enum class ScriptKind
    {
        Asset,  // compiled .vjs through the cs_script keyvalue
        Source, // raw source handed over by the plugin
        File,   // raw source read from disk, again on every run
    };

    struct Script
    {
        PluginId owner = 0;
        ScriptKind kind = ScriptKind::Asset;
        std::string path;   // asset, virtual path or file path, by kind
        std::string source; // Source only
        bool persistent = false;
        CEntityHandle handle;
        /// Spawn again on the next simulating frame (entity gone, map changed).
        bool pending = false;
    };

    class ScriptsManager : public ISource2ModScripts
    {
    public:
        bool CanRunSource() override;

        CEntityHandle LoadAsset(PluginId owner, const char* pszName, const char* pszAsset, bool bPersistent) override;
        CEntityHandle RunSource(PluginId owner, const char* pszName, const char* pszVirtualPath, const char* pszSource, bool bPersistent) override;
        CEntityHandle RunFile(PluginId owner, const char* pszName, const char* pszFilePath, bool bPersistent) override;
        CEntityHandle Reload(const char* pszName) override;
        void Remove(const char* pszName) override;
        CEntityHandle Find(const char* pszName) override;
        bool FireInput(const char* pszName, const char* pszInput) override;


        Source2ModScriptMessageHookId HookScriptMessage(const char* pszChannel, ScriptMessageHandler handler) override { const PluginId owner = hookid::OwnerOfHandler(handler); return HookScriptMessage(owner, pszChannel, std::move(handler)); }
        bool UnhookScriptMessage(const char* pszChannel, const ScriptMessageHandler& handler) override { return UnhookScriptMessage(hookid::OwnerOfHandler(handler), pszChannel, handler); }
        bool UnhookScriptMessage(Source2ModScriptMessageHookId id) override { return UnhookScriptMessageId(id); }

        // The same with the owner spelled out: what the calls above resolve
        // to, and what the core itself calls.
        Source2ModScriptMessageHookId HookScriptMessage(PluginId owner, const char* pszChannel, ScriptMessageHandler handler);
        bool UnhookScriptMessage(PluginId owner, const char* pszChannel, const ScriptMessageHandler& handler);
        bool UnhookScriptMessageId(Source2ModHookId id);

    public:
        /// Resolves the loader and registers `source2mod_script`. Not fatal: without
        /// the loader only LoadAsset() works.
        void Init();
        void Shutdown();

        void OnLevelInit();
        void OnLevelShutdown();
        void OnGameFrame(bool simulating);
        void OnEntityDeleted(CEntityInstance* pEntity);

        void RemoveAllForPlugin(PluginId id);

        /// `source2mod_script <channel> <payload>`, from the server only.
        void DispatchMessage(const char* pszChannel, const char* pszPayload);

    private:
        CEntityHandle Add(PluginId owner, const char* pszName, Script script);
        CEntityHandle Spawn(const std::string& name, Script& script);
        CBaseEntity* GetEntity(const Script& script);
        void* GetScriptComponent(CBaseEntity* pEntity);

        struct MessageHandler
        {
            PluginId owner;
            std::string channel;
            ScriptMessageHandler handler;
            Source2ModHookId id;
        };

        std::unordered_map<std::string, Script> m_scripts;
        std::vector<MessageHandler> m_handlers;

        RunScript_t m_pfnRunScript = nullptr;
        void* m_pComponentVTable = nullptr;
        int m_nComponentOffset = -1;
        bool m_bComponentChecked = false;

        /// Between OnLevelShutdown and the next OnLevelInit nothing is spawned:
        /// the entities going away then are the map's teardown. Starts false so
        /// a source2mod loaded into a running map can spawn straight away.
        bool m_bLevelShutdown = false;
    };

    extern ScriptsManager scriptsManager;
}
