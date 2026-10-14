/**
* vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * Copyright (C) 2025-2026 Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl.
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
 * As a special exception, Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
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
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */
#include "plugin.h"

#include "addresses.h"
#include "commands.h"
#include "convars.h"
#include "core/paths.h"
#include "core/scheduler.h"
#include "crashhandler.h"
#include "customhud.h"
#include "events.h"
#include "gameconfig.h"
#include "gamehooks.h"
#include "gamesystems.h"
#include "http.h"
#include "inlinehooks.h"
#include "mysql.h"
#include "networkmessages.h"
#include "patches.h"
#include "permissions.h"
#include "pluginapi.h"
#include "pluginmanager.h"
#include "raytrace.h"
#include "scripts.h"
#include "shared.h"
#include "slowguard.h"
#include "sounds.h"
#include "transmit.h"
#include "sourcepawn/scriptplugin.h"
#include "utils/log.h"
#include "utils/paths.h"
#include "virtualhooks.h"

#include <source2mod/utils/plat.h>

#define VERSION_STRING SEMVER " @ " GITHUB_SHA
#define BUILD_TIMESTAMP __DATE__ " " __TIME__

Source2ModCore g_Source2ModCore;
PLUGIN_EXPOSE(Source2Mod, g_Source2ModCore);

bool Source2ModCore::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
    PLUGIN_SAVEVARS();
    // PLUGIN_SAVEVARS() just fetched metamod's KHook; every source2mod plugin gets the
    // same engine through SOURCE2MOD_KHOOK_INTERFACE, so hooks placed anywhere on the
    // server can call through each other's originals.

    if (late)
    {
        std::snprintf(error, maxlen, "Late load is not supported");
        return false;
    }

#ifdef _WIN32
    // Everything the server prints is UTF-8, but a Windows console shows it in
    // the OEM code page (852, 437, ...) until told otherwise.
    if (GetConsoleWindow())
        SetConsoleOutputCP(CP_UTF8);
#endif

    GET_V_IFACE_CURRENT(GetServerFactory, g_pSource2Server, ISource2Server, SOURCE2SERVER_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetServerFactory, g_pSource2ServerConfig, ISource2ServerConfig, SOURCE2SERVERCONFIG_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetServerFactory, g_pSource2GameClients, ISource2GameClients, SOURCE2GAMECLIENTS_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetServerFactory, g_pSource2GameEntities, ISource2GameEntities, SOURCE2GAMEENTITIES_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pEngineServer, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pSoundSystem, ISoundSystem, SOUNDSYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pGameResourceServiceServer, IGameResourceService, GAMERESOURCESERVICESERVER_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetworkSystem, INetworkSystem, NETWORKSYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetworkMessages, INetworkMessages, NETWORKMESSAGES_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetworkStringTableServer, INetworkStringTableContainer, SOURCE2ENGINETOSERVERSTRINGTABLE_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pEngineServiceMgr, IEngineServiceMgr, ENGINESERVICEMGR_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pHostStateMgr, IHostStateMgr, HOSTSTATEMGR_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pLocalize, ILocalize, LOCALIZE_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);

    // IGameEventSystem has no global in interfaces.h at all. The schema system
    // and game resource service do, but as the interface type -- Source2Mod
    // needs the concrete classes' methods, so it downcasts once here.
    GET_V_IFACE_CURRENT(GetEngineFactory, shared::g_pGameEventSystem, IGameEventSystem, GAMEEVENTSYSTEM_INTERFACE_VERSION);

    shared::g_pSchemaSystem = static_cast<CSchemaSystem*>(g_pSchemaSystem);

    log::Init();
    scheduler::Init();

    // Before anything below goes looking for one of them.
    paths::EnsureLayout();

    auto coreconfig_path = std::string(paths::GetConfigsDirectory() + "/core");
    shared::g_pCoreConfig = new CCoreConfig(coreconfig_path);
    char coreconfig_error[255] = "";

    if (!shared::g_pCoreConfig->Init(coreconfig_error, sizeof(coreconfig_error)))
    {
        FP_ERROR("Failed to load CCoreConfig. Could not read \'{}\'. Error: {}", coreconfig_path, coreconfig_error);
        return false;
    }

    // As soon as the config allows and before anything else: whatever below
    // this line dies, plugins included, is caught and reported next start.
    crashhandler::Init();

    auto gamedata_folder = paths::GetGamedataDirectory();
    shared::g_pGameConfig = new CGameConfig(gamedata_folder);
    char conf_error[255] = {};

    if (!shared::g_pGameConfig->InitAll(gamedata_folder, conf_error, sizeof(conf_error)))
    {
        FP_ERROR("Failed to load CGameConfig. Error: {}", conf_error);
        return false;
    }

    // Before anything below can reach SDK code that reads them.
    FillSource2ModGlobals();

    if (!addresses::Initialize())
    {
        FP_ERROR("Could not initialize addresses.");
        return false;
    }

    // Not fatal: a failed scan only means plugins cannot register game
    // systems, which most do not. ISource2ModGameSystems::IsAvailable() says so.
    gamesystems::gameSystemsManager.Init();

    // Not fatal either: without the engine's sound system the guids come from
    // a counter of Source2Mod's own.
    sounds::soundsManager.Init();

    if (!patches::Initialize())
    {
        FP_ERROR("Could not initialize patches.");
        return false;
    }

    // Before InitCommands: a ConCommand made before ConVar_Register goes on
    // tier1's registration list, and ConVar_Unregister walks that list. The
    // source2mod's commands are heap objects DestructCommands deletes first, so
    // the walk would read freed memory (SIGSEGV in UnregisterConCommandCallbacks
    // on unload). Made after it, they register directly and stay off the list.
    ConVar_Register(FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE | FCVAR_GAMEDLL);

    commands::InitCommands();
    slow::g_flWarnMs = shared::g_pCoreConfig->SlowCallbackWarnMs;

    // Before plugins load: their Load() may already grant and check.
    permissions::permissionsManager.Init();

    // Not fatal: without the script loader, compiled assets still run.
    scripts::scriptsManager.Init();

    commands::commandsManager.UnlockConCommands();
    convars::convarsManager.UnlockConVars();
    inlinehooks::inlines.InitListeners();
    virtualhooks::virtuals.InitListeners();

    g_SMAPI->AddListener(this, this);

    FP_INFO("Load() success!");

    if (!pluginManager.LoadAll())
    {
        FP_ERROR("Could not load plugins.");
        return false;
    }

    pluginManager.StartFileWatcher();

    return true;
}

bool Source2ModCore::Unload(char* error, size_t maxlen)
{
    pluginManager.StopFileWatcher();

    // Whatever a plugin registered that holds code inside its own library --
    // timers, HTTP callbacks, database connections, entity listeners, open
    // menus -- goes with that plugin, one at a time, while it is still
    // mapped. See the RemoveAllForPlugin() calls in PluginManager.
    pluginManager.UnloadAll();

    commands::DestructCommands();
    events::DestructEvents();
    // None of the hooked game functions is on the stack here, so these come
    // out synchronously like the rest.
    gamehooks::gameHooksManager.Shutdown();
    inlinehooks::inlines.DestructListeners();
    virtualhooks::virtuals.DestructListeners();
    // Only Source2Mod's own are left by now.
    scheduler::Shutdown();
    // After the scheduler: a next-frame close still holding a script runtime
    // drops it there, and the runtimes have to go before the VM.
    sourcepawn::Shutdown();
    // Takes the engine-level change callback back out with it.
    convars::convarsManager.Shutdown();
    http::httpManager.Shutdown();
    // Takes the Steam ticket callback back out.
    permissions::permissionsManager.Shutdown();
    // Joins the worker threads. They run code from this library, so leaving
    // one running is a crash the moment Metamod unmaps it.
    mysql::mysqlManager.Shutdown();

    if (shared::g_pEntitySystem)
        shared::g_pEntitySystem->RemoveListenerEntity(&virtualhooks::entityListener);

    shared::g_bDetoursLoaded = false;

    ConVar_Unregister();

    // Last: a crash anywhere in the teardown above is still worth a dump.
    crashhandler::Shutdown();

    FP_INFO("Unload() success!");

    log::Close();

    return true;
}

void Source2ModCore::AllPluginsLoaded()
{
    pluginManager.FireMetamodLoaded();
}

// These two are IMetamodListener's, so the id is a Metamod plugin's -- they go
// to Source2Mod's own OnMetamodPluginLoad/Unload, not to the callbacks that
// report .smx plugins coming and going.
void Source2ModCore::OnPluginLoad(PluginId id)
{
    pluginManager.FireMetamodPluginLoaded(id);
}

void Source2ModCore::OnPluginUnload(PluginId id)
{
    pluginManager.FireMetamodPluginUnloaded(id);
}

void Source2ModCore::OnLevelInit(char const* pMapName, char const* pMapEntities, char const* pOldLevel,
                              char const* pLandmarkName, bool loadGame, bool background)
{
    scripts::scriptsManager.OnLevelInit();

    pluginManager.OnLevelInit(pMapName, pMapEntities, pOldLevel, pLandmarkName, loadGame, background);
}

void* Source2ModCore::OnMetamodQuery(const char* iface, int* ret)
{
    // Only what a source2mod plugin chose to expose through OnMetamodQuery, which
    // answers like OnSource2ModQuery unless the plugin says otherwise. The
    // source2mod's own interfaces are deliberately not offered here: they are for
    // source2mod plugins; a metamod plugin already has KHook from metamod itself.
    for (auto& p : pluginManager.m_plugins)
    {
        for (auto* l : p->listeners)
        {
            if (void* res = l->OnMetamodQuery(iface, ret))
            {
                if (ret) *ret = META_IFACE_OK;
                return res;
            }
        }
    }

    if (ret) *ret = META_IFACE_FAILED;
    return nullptr;
}

void Source2ModCore::OnLevelShutdown()
{
    pluginManager.OnLevelShutdown();

    // The layouts these point at do not survive the level change.
    customhud::customHudManager.Clear();

    // Neither do the sounds that were playing.
    sounds::soundsManager.Clear();

    // Nor the entities whose visibility was being managed.
    transmit::transmitManager.Clear();

    // Nor the scripts' entities; the persistent ones come back on the
    // next map.
    scripts::scriptsManager.OnLevelShutdown();
}

const char* Source2ModCore::GetAuthor() { return reinterpret_cast<const char*>(u8"Michal \"Slynx (˙·٠● S l y n x ●٠·˙)\" Přikryl"); }
const char* Source2ModCore::GetName() { return "Source2Mod"; }
const char* Source2ModCore::GetDescription() { return "Source2Mod"; }
const char* Source2ModCore::GetURL() { return "https://www.slynxdev.cz"; }
const char* Source2ModCore::GetLicense() { return "Source2Mod (GPLv3 with exceptions)"; }
const char* Source2ModCore::GetVersion() { return VERSION_STRING; }
const char* Source2ModCore::GetDate() { return BUILD_TIMESTAMP; }
const char* Source2ModCore::GetLogTag() { return "Source2Mod"; }
