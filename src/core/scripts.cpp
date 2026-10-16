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
#include "scripts.h"

#include "addresses.h"
#include "commands.h"
#include "entities.h"
#include "gameconfig.h"
#include "hookid.h"
#include "paths.h"
#include "shared.h"
#include "utils/log.h"

#include <ISmmPlugin.h>
#include <dynlibutils/module.hpp>
#include <entity2/entityinstance.h>
#include <entity2/entitysystem.h>
#include <entitykeyvalues.h>
#include <interfaces/interfaces.h>
#include <source2mod/schema/entity/classes/CBaseEntity.h>
#include <tier0/convar.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace scripts
{
    ScriptsManager scriptsManager;

    /* =========================
    Helpers
    ========================= */

    namespace
    {
        constexpr const char* SCRIPT_CLASSNAME = "point_script";
        constexpr const char* COMPONENT_VTABLE = "CCSScript_EntityScript";

        /// How far around the gamedata offset the component's vtable is looked
        /// for. The component is larger than this (its second vtable pointer
        /// sits at +0xF0), so the window never reads past the entity.
        constexpr int COMPONENT_SCAN_BEFORE = 0x300;
        constexpr int COMPONENT_SCAN_AFTER = 0xF0;

        bool ReadFile(const std::string& path, std::string& out)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return false;

            std::ostringstream ss;
            ss << file.rdbuf();
            out = ss.str();
            return true;
        }

        std::string ResolvePath(const std::string& path)
        {
            std::filesystem::path p(path);
            if (p.is_relative())
                p = std::filesystem::path(paths::pathsManager.GameDirectory()) / p;

            return p.string();
        }

        bool EndsWith(const std::string& s, const char* suffix)
        {
            const size_t n = strlen(suffix);
            return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
        }

        /// RunScript appends "_c" to the path and names the module ".js" only
        /// when that gives ".vjs_c"; hand it "<name>.vjs" whatever was written.
        std::string ModulePath(std::string path)
        {
            for (const char* ext : { ".vjs_c", ".vjs", ".js" })
            {
                if (EndsWith(path, ext))
                {
                    path.resize(path.size() - strlen(ext));
                    break;
                }
            }

            return path + ".vjs";
        }

        /// ArgS() without its first argument (the channel, maybe quoted).
        const char* SkipFirstArg(const char* s)
        {
            while (*s == ' ' || *s == '\t')
                ++s;

            if (*s == '"')
            {
                ++s;
                while (*s && *s != '"')
                    ++s;
                if (*s == '"')
                    ++s;
            }
            else
            {
                while (*s && *s != ' ' && *s != '\t')
                    ++s;
            }

            while (*s == ' ' || *s == '\t')
                ++s;

            return s;
        }

        void* At(CBaseEntity* pEntity, int offset)
        {
            return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(pEntity) + offset);
        }
    }

    // Scripts send this with Instance.ServerCommand, which goes through the
    // server's command buffer -- slot -1. Anything a client sends (the console,
    // or the "!source2mod_script" chat alias) is not a script and is dropped.
    static void HandleScriptCommand(const Source2ModCommandContext& ctx, const Source2ModCommandArgs& args, bool post)
    {
        if (ctx.GetPlayerSlot().Get() != -1)
            return;

        if (args.ArgC() < 2)
            return;

        scriptsManager.DispatchMessage(args.Arg(1), SkipFirstArg(args.ArgS()));
    }

    // bool FilterWorkshopCommands(const char* pszCommands, CBufferString* pOut):
    // what Instance.ServerCommand runs the script's text through. While a
    // workshop addon is mounted it rebuilds the text into pOut without every
    // command and ConVar a workshop map may not use ("DISALLOWED WORKSHOP
    // COMMANDS: source2mod_script") and returns true; returning false makes
    // the caller run the text as it was. A lone source2mod_script is let
    // through that way, so a script on a workshop map still reaches
    // HandleScriptCommand. Text with a command separator is left to the
    // filter: only that one command bypasses it.
    static KHook::Function<bool, const char*, void*>* s_hFilterWorkshopCommands = nullptr;

    static bool IsLoneScriptCommand(const char* s)
    {
        if (!s)
            return false;
        while (*s == ' ' || *s == '\t')
            ++s;
        static constexpr char kCommand[] = "source2mod_script";
        if (std::strncmp(s, kCommand, sizeof(kCommand) - 1) != 0)
            return false;
        s += sizeof(kCommand) - 1;
        if (*s != ' ' && *s != '\t')
            return false;
        return std::strpbrk(s, ";\n\r") == nullptr;
    }

    static KHook::Return<bool> Hook_FilterWorkshopCommands(const char* pszCommands, void* pOut)
    {
        if (IsLoneScriptCommand(pszCommands))
            return { KHook::Action::Supersede, false };
        return { KHook::Action::Ignore, false };
    }

    /* =========================
    Setup
    ========================= */

    void ScriptsManager::Init()
    {
        m_pfnRunScript = addresses::FindSignature("CSScript::RunScript").RCast<RunScript_t>();
        if (!m_pfnRunScript)
            FP_WARN("CSScript::RunScript not found: scripts can only be loaded as compiled assets");

        m_pComponentVTable = DynLibUtils::CModule(g_pSource2Server).GetVirtualTableByName(COMPONENT_VTABLE).RCast<void*>();

        commands::commandsManager.RegisterConCommand(0, "source2mod_script", HandleScriptCommand);

        if (void* pFilter = addresses::FindSignature("FilterWorkshopCommands").GetPtr())
        {
            s_hFilterWorkshopCommands = new KHook::Function<bool, const char*, void*>(&Hook_FilterWorkshopCommands, nullptr);
            s_hFilterWorkshopCommands->Configure(pFilter);
        }
        else
            FP_WARN("FilterWorkshopCommands not found: scripts on a workshop map cannot message plugins");
    }

    void ScriptsManager::Shutdown()
    {
        delete s_hFilterWorkshopCommands;
        s_hFilterWorkshopCommands = nullptr;
    }

    bool ScriptsManager::CanRunSource()
    {
        if (!m_pfnRunScript)
            return false;

        // Settled on the first spawn; until then, whether there is anything to
        // find the component with.
        if (m_bComponentChecked)
            return m_nComponentOffset >= 0;

        return m_pComponentVTable || shared::g_pGameConfig->GetOffset("CCSPointScriptEntity::ScriptComponent") >= 0;
    }

    void* ScriptsManager::GetScriptComponent(CBaseEntity* pEntity)
    {
        if (!m_bComponentChecked)
        {
            m_bComponentChecked = true;

            const int gamedataOffset = shared::g_pGameConfig->GetOffset("CCSPointScriptEntity::ScriptComponent");

            if (!m_pComponentVTable)
            {
                // Nothing to check it against: the gamedata has the last word.
                m_nComponentOffset = gamedataOffset;
            }
            else if (gamedataOffset >= 0 && At(pEntity, gamedataOffset) == m_pComponentVTable)
            {
                m_nComponentOffset = gamedataOffset;
            }
            else if (gamedataOffset >= 0)
            {
                // The entity moved its members around; the component is still
                // close by and still starts with its own vtable.
                const int begin = std::max(0, gamedataOffset - COMPONENT_SCAN_BEFORE);
                const int end = gamedataOffset + COMPONENT_SCAN_AFTER;

                for (int offset = begin; offset <= end; offset += sizeof(void*))
                {
                    if (At(pEntity, offset) == m_pComponentVTable)
                    {
                        m_nComponentOffset = offset;
                        FP_WARN("CCSPointScriptEntity::ScriptComponent is stale ({:#x}), found the component at {:#x}", gamedataOffset, offset);
                        break;
                    }
                }
            }

            if (m_nComponentOffset < 0)
                FP_ERROR("Could not find the script component in point_script: scripts can only be loaded as compiled assets");
        }

        if (m_nComponentOffset < 0)
            return nullptr;

        if (m_pComponentVTable && At(pEntity, m_nComponentOffset) != m_pComponentVTable)
            return nullptr;

        return reinterpret_cast<uint8_t*>(pEntity) + m_nComponentOffset;
    }

    /* =========================
    Scripts
    ========================= */

    CBaseEntity* ScriptsManager::GetEntity(const Script& script)
    {
        if (!shared::g_pEntitySystem || !script.handle.IsValid())
            return nullptr;

        return static_cast<CBaseEntity*>(shared::g_pEntitySystem->GetEntityInstance(script.handle));
    }

    CEntityHandle ScriptsManager::Spawn(const std::string& name, Script& script)
    {
        script.handle = CEntityHandle();
        script.pending = false;

        // No world to put it in yet; the next simulating frame does it.
        if (!shared::g_pEntitySystem || m_bLevelShutdown)
        {
            script.pending = true;
            return {};
        }

        std::string fileSource;

        if (script.kind != ScriptKind::Asset)
        {
            if (!CanRunSource())
            {
                FP_ERROR("Script '{}': raw source cannot be run on this game version", name);
                return {};
            }

            if (script.kind == ScriptKind::File && !ReadFile(ResolvePath(script.path), fileSource))
            {
                FP_ERROR("Script '{}': cannot read '{}'", name, ResolvePath(script.path));
                return {};
            }
        }

        CBaseEntity* pEntity = entities::entitiesManager.CreateEntityByName(SCRIPT_CLASSNAME);
        if (!pEntity)
        {
            FP_ERROR("Script '{}': could not create {}", name, SCRIPT_CLASSNAME);
            return {};
        }

        auto* pKeyValues = new CEntityKeyValues();
        pKeyValues->SetString("targetname", name.c_str());

        // Without the keyvalue the game prints 'Failed to load cs_script ""'
        // while spawning; harmless, the source goes into the same component
        // right after, exactly the call the spawn would have made.
        if (script.kind == ScriptKind::Asset)
            pKeyValues->SetString("cs_script", script.path.c_str());

        pEntity->DispatchSpawn(pKeyValues);

        if (script.kind != ScriptKind::Asset)
        {
            void* pComponent = GetScriptComponent(pEntity);
            if (!pComponent)
            {
                pEntity->Remove();
                return {};
            }

            const std::string& source = script.kind == ScriptKind::File ? fileSource : script.source;
            m_pfnRunScript(pComponent, ModulePath(script.path).c_str(), source.c_str());
        }

        script.handle = pEntity->GetRefEHandle();
        return script.handle;
    }

    CEntityHandle ScriptsManager::Add(PluginId owner, const char* pszName, Script script)
    {
        if (!pszName || !*pszName)
            return {};

        const std::string name = pszName;

        Remove(name.c_str());

        script.owner = owner;
        Script& stored = m_scripts[name] = std::move(script);

        CEntityHandle handle = Spawn(name, stored);

        // Failed outright (not merely waiting for a world): nothing to keep.
        if (!handle.IsValid() && !stored.pending)
            m_scripts.erase(name);

        return handle;
    }

    CEntityHandle ScriptsManager::LoadAsset(PluginId owner, const char* pszName, const char* pszAsset, bool bPersistent)
    {
        if (!pszAsset || !*pszAsset)
            return {};

        Script script;
        script.kind = ScriptKind::Asset;
        script.path = pszAsset;
        script.persistent = bPersistent;
        return Add(owner, pszName, std::move(script));
    }

    CEntityHandle ScriptsManager::RunSource(PluginId owner, const char* pszName, const char* pszVirtualPath, const char* pszSource, bool bPersistent)
    {
        if (!pszSource)
            return {};

        Script script;
        script.kind = ScriptKind::Source;
        script.path = pszVirtualPath && *pszVirtualPath ? pszVirtualPath : (std::string("scripts/") + (pszName ? pszName : ""));
        script.source = pszSource;
        script.persistent = bPersistent;
        return Add(owner, pszName, std::move(script));
    }

    CEntityHandle ScriptsManager::RunFile(PluginId owner, const char* pszName, const char* pszFilePath, bool bPersistent)
    {
        if (!pszFilePath || !*pszFilePath)
            return {};

        Script script;
        script.kind = ScriptKind::File;
        script.path = pszFilePath;
        script.persistent = bPersistent;
        return Add(owner, pszName, std::move(script));
    }

    CEntityHandle ScriptsManager::Reload(const char* pszName)
    {
        if (!pszName)
            return {};

        auto it = m_scripts.find(pszName);
        if (it == m_scripts.end())
            return {};

        Script copy = it->second;
        return Add(copy.owner, std::string(pszName).c_str(), std::move(copy));
    }

    void ScriptsManager::Remove(const char* pszName)
    {
        if (!pszName)
            return;

        auto it = m_scripts.find(pszName);
        if (it == m_scripts.end())
            return;

        // Out of the registry first, so OnEntityDeleted does not bring it back.
        CBaseEntity* pEntity = GetEntity(it->second);
        m_scripts.erase(it);

        if (pEntity)
            pEntity->Remove();
    }

    CEntityHandle ScriptsManager::Find(const char* pszName)
    {
        if (!pszName)
            return {};

        auto it = m_scripts.find(pszName);
        if (it == m_scripts.end() || !GetEntity(it->second))
            return {};

        return it->second.handle;
    }

    bool ScriptsManager::FireInput(const char* pszName, const char* pszInput)
    {
        if (!pszName || !pszInput)
            return false;

        auto it = m_scripts.find(pszName);
        if (it == m_scripts.end())
            return false;

        CBaseEntity* pEntity = GetEntity(it->second);
        if (!pEntity)
            return false;

        entities::entitiesManager.AcceptInput(pEntity, "RunScriptInput", nullptr, nullptr, pszInput);
        return true;
    }

    /* =========================
    Messages
    ========================= */

    Source2ModScriptMessageHookId ScriptsManager::HookScriptMessage(PluginId owner, const char* pszChannel, ScriptMessageHandler handler)
    {
        if (!pszChannel || !*pszChannel || !handler)
            return 0;

        const Source2ModHookId id = hookid::Next();
        m_handlers.push_back(MessageHandler{ owner, pszChannel, std::move(handler), id });
        return id;
    }

    bool ScriptsManager::UnhookScriptMessage(PluginId owner, const char* pszChannel, const ScriptMessageHandler& handler)
    {
        if (!pszChannel || !handler.HasIdentity())
            return false;

        return std::erase_if(m_handlers, [&](const MessageHandler& e)
        {
            return e.owner == owner && e.channel == pszChannel && handler.SameAs(e.handler);
        }) > 0;
    }

    bool ScriptsManager::UnhookScriptMessageId(Source2ModHookId id)
    {
        return std::erase_if(m_handlers, [id](const MessageHandler& e) { return e.id == id; }) > 0;
    }

    void ScriptsManager::DispatchMessage(const char* pszChannel, const char* pszPayload)
    {
        // Copied out: a handler may register or unregister while it runs.
        std::vector<ScriptMessageHandler> targets;
        for (const auto& entry : m_handlers)
        {
            if (entry.channel == pszChannel)
                targets.push_back(entry.handler);
        }

        if (targets.empty())
        {
            FP_DEBUG("source2mod_script: nobody listens on '{}'", pszChannel);
            return;
        }

        for (auto& handler : targets)
            handler(pszChannel, pszPayload ? pszPayload : "");
    }

    /* =========================
    Lifecycle
    ========================= */

    void ScriptsManager::OnLevelInit()
    {
        m_bLevelShutdown = false;
    }

    void ScriptsManager::OnLevelShutdown()
    {
        m_bLevelShutdown = true;

        // The map takes every entity with it. Persistent scripts come back on
        // the next map's first simulating frame, the rest are done.
        for (auto it = m_scripts.begin(); it != m_scripts.end();)
        {
            if (!it->second.persistent)
            {
                it = m_scripts.erase(it);
                continue;
            }

            it->second.handle = CEntityHandle();
            it->second.pending = true;
            ++it;
        }
    }

    void ScriptsManager::OnGameFrame(bool simulating)
    {
        if (!simulating || m_bLevelShutdown || !shared::g_pEntitySystem || m_scripts.empty())
            return;

        for (auto& [name, script] : m_scripts)
        {
            // A failed spawn clears pending, so a broken script is reported
            // once, not every frame; Reload() tries again.
            if (script.pending)
                Spawn(name, script);
        }
    }

    void ScriptsManager::OnEntityDeleted(CEntityInstance* pEntity)
    {
        if (m_scripts.empty() || !pEntity)
            return;

        const CEntityHandle handle = pEntity->GetRefEHandle();

        for (auto it = m_scripts.begin(); it != m_scripts.end();)
        {
            if (it->second.handle != handle)
            {
                ++it;
                continue;
            }

            // Round restart, a map's own cleanup, somebody's ent_remove. Not
            // spawned here: the entity system is in the middle of deleting.
            if (it->second.persistent)
            {
                it->second.handle = CEntityHandle();
                it->second.pending = true;
                ++it;
            }
            else
            {
                it = m_scripts.erase(it);
            }
        }
    }

    void ScriptsManager::RemoveAllForPlugin(PluginId id)
    {
        std::vector<std::string> owned;
        for (const auto& [name, script] : m_scripts)
        {
            if (script.owner == id)
                owned.push_back(name);
        }

        for (const auto& name : owned)
            Remove(name.c_str());

        std::erase_if(m_handlers, [id](const MessageHandler& e) { return e.owner == id; });
    }
}
