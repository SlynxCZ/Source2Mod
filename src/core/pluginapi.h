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
#pragma once

#include <source2mod/ISource2ModApi.h>

#include <memory>
#include <string>
#include <vector>

struct Source2ModPlugin;

class PluginApi : public ISource2ModAPI
{
public:
    /// The plugin whose Load() is running: Source2ModFactory records what it
    /// asks for and names it in its messages. Null outside a Load().
    static void SetLoadingPlugin(Source2ModPlugin* plugin);

    /// Whether Source2ModFactory() would hand an interface out, without its logging (ISource2ModFeatures).
    static bool IsInterfaceServed(const char* iface);

    void Log(ISource2ModPlugin *plugin, const char* msg, ...) override;
    void ConPrint(const char* msg) override;
    void ConPrintf(const char* fmt, ...) override;

    void AddListener(ISource2ModPlugin *plugin, ISource2ModListener *pListener) override;

    void* QueryInterface(CreateInterfaceFn fn, const char *iface, int min) override;
    void* InterfaceSearch(CreateInterfaceFn fn, const char *iface, int max, int *ret) override;
    int FormatIface(char iface[], size_t maxlength) override;

    CreateInterfaceFn GetEngineFactory(bool syn=true) override;
    CreateInterfaceFn GetPhysicsFactory(bool syn=true) override;
    CreateInterfaceFn GetFileSystemFactory(bool syn=true) override;
    CreateInterfaceFn GetServerFactory(bool syn=true) override;

    void* Source2ModFactory(const char *iface, int *ret, PluginId *id) override;
    void* MetaFactory(const char *iface, int *ret, PluginId *id) override;

    ISource2ModAddresses* Addresses() override;
    ISource2ModCommands* Commands() override;
    ISource2ModConVars* ConVars() override;
    ISource2ModCustomHud* CustomHud() override;
    ISource2ModEntities* Entities() override;
    ISource2ModEvents* Events() override;
    ISource2ModGameConfig* GameConfig() override;
    ISource2ModGameSystems* GameSystems() override;
    ISource2ModHTTP* HTTP() override;
    ISource2ModMenus* Menus() override;
    ISource2ModSQL* SQL() override;
    ISource2ModNetworkMessages* NetworkMessages() override;
    ISource2ModScheduler* Scheduler() override;
    ISource2ModSounds* Sounds() override;
    ISource2ModTrace* Trace() override;
    ISource2ModTransmit* Transmit() override;
    ISource2ModGameHooks* GameHooks() override;
    ISource2ModPaths* Paths() override;

    ISource2ModModule* LoadModule(const char* name) override;
    ISource2ModModule* LoadModuleFromMemory(uintptr_t ptr) override;
    void FreeModule(ISource2ModModule* module) override;

    ISource2Server* GetSource2Server() override;
    ISource2ServerConfig* GetSource2ServerConfig() override;
    ISource2GameClients* GetSource2GameClients() override;
    ISource2GameEntities* GetSource2GameEntities() override;
    IVEngineServer2* GetEngineServer() override;
    ICvar* GetCVar() override;
    CSchemaSystem* GetSchemaSystem() override;
    IGameResourceService* GetGameResourceService() override;
    INetworkServerService* GetNetworkServerService() override;
    INetworkSystem* GetNetworkSystem() override;
    INetworkMessages* GetNetworkMessages() override;
    INetworkStringTableContainer* GetNetworkStringTableServer() override;
    IEngineServiceMgr* GetEngineServiceMgr() override;
    IHostStateMgr* GetHostStateMgr() override;
    ILocalize* GetLocalize() override;
    IFileSystem* GetFileSystem() override;

    IGameEventSystem* GetGameEventSystem() override;
    IGameEventManager2* GetGameEventManager() override;
    CGameEntitySystem* GetEntitySystem() override;
    CGlobalVars* GetGlobalVars() override;

    const char* GetBaseDir() override;

    size_t Format(char *buffer, size_t maxlength, const char *format, ...) override;
    size_t FormatArgs(char *buffer, size_t maxlength, const char *format, va_list ap) override;
};

extern PluginApi pluginApi;

/// Points the SDK's source2mod globals (g_Source2ModAPI, g_pSource2ModAddresses, ...)
/// at the core's own interfaces. Call once the game config exists: the
/// game-config interface is the only one that is not a static object.
void FillSource2ModGlobals();