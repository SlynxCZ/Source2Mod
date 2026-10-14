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
#include "pluginapi.h"

#include "addresses.h"
#include "commands.h"
#include "convars.h"
#include "core/paths.h"
#include "core/scheduler.h"
#include "customhud.h"
#include "entities.h"
#include "events.h"
#include "gamehooks.h"
#include "gamesystems.h"
#include "http.h"
#include "json.h"
#include "menus.h"
#include "module.h"
#include "mysql.h"
#include "networkmessages.h"
#include "permissions.h"
#include "plugin.h"
#include "pluginmanager.h"
#include "raytrace.h"
#include "scripts.h"
#include "sounds.h"
#include "transmit.h"
#include "sourcepawn/natives.h"
#include "utils/log.h"

#include <source2mod/ISource2ModAddresses.h>
#include <source2mod/ISource2ModCommands.h>
#include <source2mod/ISource2ModConVars.h>
#include <source2mod/ISource2ModCustomHud.h>
#include <source2mod/ISource2ModEntities.h>
#include <source2mod/ISource2ModEvents.h>
#include <source2mod/ISource2ModGameConfig.h>
#include <source2mod/ISource2ModGameSystems.h>
#include <source2mod/ISource2ModHTTP.h>
#include <source2mod/ISource2ModMenus.h>
#include <source2mod/ISource2ModMySQL.h>
#include <source2mod/ISource2ModNatives.h>
#include <source2mod/ISource2ModNetworkMessages.h>
#include <source2mod/ISource2ModPlugin.h>
#include <source2mod/ISource2ModScheduler.h>
#include <source2mod/ISource2ModSounds.h>
#include <source2mod/ISource2ModTrace.h>
#include <source2mod/ISource2ModTransmit.h>

#include <cstring>
#include <stdarg.h>

PluginApi pluginApi;

// The same globals a plugin gets from SOURCE2MOD_EXPOSE, so the SDK code linked
// into the core (the schema helpers, ADDR_*, ...) reads them exactly as it
// does inside a plugin. Filled by FillSource2ModGlobals().
SOURCE2MOD_DEFINE_GLOBALVARS()

void FillSource2ModGlobals()
{
    g_Source2ModAPI = &pluginApi;
    SOURCE2MOD_FILLVARS(&pluginApi)
}

void PluginApi::Log(ISource2ModPlugin* plugin, const char* msg, ...)
{
    va_list ap;
    char buffer[2048];

    va_start(ap, msg);
    FormatArgs(buffer, sizeof(buffer), msg, ap);
    va_end(ap);

    ConPrintf("[%s] %s", plugin->GetName(), buffer);
}

void PluginApi::ConPrint(const char* msg)
{
    g_SMAPI->ConPrint(msg);
}

void PluginApi::ConPrintf(const char* fmt, ...)
{
    va_list ap;
    char buffer[2048];

    va_start(ap, fmt);
    FormatArgs(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);

    g_SMAPI->ConPrint(buffer);
}

void PluginApi::AddListener(ISource2ModPlugin* plugin, ISource2ModListener* pListener)
{
    pluginManager.AddListener(plugin, pListener);
}

void* PluginApi::QueryInterface(CreateInterfaceFn fn, const char* iface, int min)
{
    char buffer[256]; /* assume no interface will go beyond this */
    size_t len = strlen(iface);
    int ret; /* just in case something doesn't handle NULL properly */

    if (len > sizeof(buffer) - 4)
    {
        return NULL;
    }

    strcpy(buffer, iface);

    if (min != -1)
    {
        char* ptr = &buffer[len - 1];
        int digits = 0;
        while (isdigit(*ptr) && digits <= 3)
        {
            *ptr = '\0';
            digits++;
            ptr--;
        }
        if (digits != 3)
        {
            /* for now, assume this is an error */
            strcpy(buffer, iface);
        }
        else
        {
            char num[4];
            min = (min == 0) ? 1 : min;
            snprintf(num, sizeof(num), "%03d", min);
            strcat(buffer, num);
        }
    }

    return InterfaceSearch(fn, buffer, IFACE_MAXNUM, &ret);
}

void* PluginApi::InterfaceSearch(CreateInterfaceFn fn, const char* iface, int max, int* ret)
{
    char _if[256]; /* assume no interface goes beyond this */
    size_t len = strlen(iface);
    int num = 0;
    void* pf = NULL;

    if (max > 999)
    {
        max = 999;
    }

    if (len + 4 > sizeof(_if))
    {
        if (ret)
        {
            *ret = META_IFACE_FAILED;
        }
        return NULL;
    }

    strcpy(_if, iface);

    do
    {
        if ((pf = (fn)(_if, ret)) != NULL)
        {
            break;
        }
        if (num > max)
        {
            break;
        }
    }
    while ((num = FormatIface(_if, len + 1)));

    return pf;
}

int PluginApi::FormatIface(char iface[], size_t maxlength)
{
    size_t length = strlen(iface);
    size_t i;
    int num = 0;

    for (i = length - 1; i + 1 > 0; i--)
    {
        if (!isdigit(iface[i]))
        {
            if (i != length - 1)
            {
                num = 1;
            }
            break;
        }
    }

    if ((num && (maxlength <= length)) || (!num && (maxlength <= length + 3)))
    {
        return -1;
    }

    if (i != length - 1)
    {
        num = atoi(&(iface[++i]));
    }

    num++;

    snprintf(&(iface[i]), 4, "%03d", num);

    return num;
}

CreateInterfaceFn PluginApi::GetEngineFactory(bool syn/* =true */)
{
    return g_SMAPI->GetEngineFactory(syn);
}

CreateInterfaceFn PluginApi::GetPhysicsFactory(bool syn/* =true */)
{
    return g_SMAPI->GetPhysicsFactory(syn);
}

CreateInterfaceFn PluginApi::GetFileSystemFactory(bool syn/* =true */)
{
    return g_SMAPI->GetFileSystemFactory(syn);
}

CreateInterfaceFn PluginApi::GetServerFactory(bool syn/* =true */)
{
    return g_SMAPI->GetServerFactory(syn);
}

static Source2ModPlugin* s_pLoadingPlugin = nullptr;

void PluginApi::SetLoadingPlugin(Source2ModPlugin* plugin)
{
    s_pLoadingPlugin = plugin;
}

namespace
{
    struct CurrentInterface
    {
        const char* iface;
        void* ptr;
    };

    // Every source2mod interface at its current revision. Built on first use,
    // after the core's Load() filled the pointers in.
    const CurrentInterface* CurrentInterfaces(size_t& count)
    {
        static const CurrentInterface s_current[] = {
            { SOURCE2MOD_ADDRESSES_INTERFACE, &addresses::source2modAddresses },
            { SOURCE2MOD_COMMANDS_INTERFACE, &commands::commandsManager },
            { SOURCE2MOD_CONVARS_INTERFACE, &convars::convarsManager },
            { SOURCE2MOD_CUSTOMHUD_INTERFACE, &customhud::customHudManager },
            { SOURCE2MOD_ENTITIES_INTERFACE, &entities::entitiesManager },
            { SOURCE2MOD_EVENTS_INTERFACE, &events::eventManager },
            { SOURCE2MOD_GAMECONFIG_INTERFACE, shared::g_pGameConfig },
            { SOURCE2MOD_GAMESYSTEMS_INTERFACE, &gamesystems::gameSystemsManager },
            { SOURCE2MOD_GAMEHOOKS_INTERFACE, &gamehooks::gameHooksManager },
            { SOURCE2MOD_HTTP_INTERFACE, &http::httpManager },
            { SOURCE2MOD_JSON_INTERFACE, &json::jsonManager },
            { SOURCE2MOD_MENUS_INTERFACE, &menus::menuManager },
            { SOURCE2MOD_MYSQL_INTERFACE, &mysql::mysqlManager },
            { SOURCE2MOD_NETWORKMESSAGES_INTERFACE, &networkmessages::networkMessagesManager },
            { SOURCE2MOD_SCHEDULER_INTERFACE, &scheduler::schedulerManager },
            { SOURCE2MOD_SCRIPTS_INTERFACE, &scripts::scriptsManager },
            { SOURCE2MOD_SOUNDS_INTERFACE, &sounds::soundsManager },
            { SOURCE2MOD_TRACE_INTERFACE, &raytrace::rayTrace },
            { SOURCE2MOD_TRANSMIT_INTERFACE, &transmit::transmitManager },
            { SOURCE2MOD_PATHS_INTERFACE, &paths::pathsManager },
            { SOURCE2MOD_PERMISSIONS_INTERFACE, &permissions::permissionsManager },
            { SOURCE2MOD_NATIVES_INTERFACE, sourcepawn::NativesInterface() },
        };
        count = sizeof(s_current) / sizeof(s_current[0]);
        return s_current;
    }

    // "ISource2ModMenus002" -> revision 2, and the length of the name before it.
    bool SplitRevision(const char* iface, size_t& nameLen, int& revision)
    {
        const size_t len = strlen(iface);
        if (len < 4)
            return false;
        for (size_t i = len - 3; i < len; i++)
            if (iface[i] < '0' || iface[i] > '9')
                return false;
        nameLen = len - 3;
        revision = atoi(iface + nameLen);
        return true;
    }
}

void* PluginApi::Source2ModFactory(const char* iface, int* ret, PluginId* id)
{
    void* ptr = nullptr;
    const char* who = s_pLoadingPlugin && s_pLoadingPlugin->api ? s_pLoadingPlugin->api->GetName() : "A plugin";

    // The one detour engine on the server, as metamod handed it to Source2Mod.
    if (!strcmp(iface, SOURCE2MOD_KHOOK_INTERFACE)) ptr = KHook::__exported__khook;
    // ... and which KHook that is: a hash of the khook.hpp the core was
    // compiled against (metamod's third_party/khook, the engine itself -- see
    // the SDK's CMakeLists.txt). A plugin's KHOOK_INIT() compares it with the
    // SDK's vendor/khook it was built from and refuses to load on a mismatch.
    else if (!strcmp(iface, SOURCE2MOD_KHOOK_HEADER_INTERFACE))
    {
        static const char s_khookHeader[] = SOURCE2MOD_KHOOK_HEADER;
        ptr = const_cast<char*>(s_khookHeader);
    }
    // The commit, for plugins built before the header hash.
    else if (!strcmp(iface, SOURCE2MOD_KHOOK_VERSION_INTERFACE))
    {
        static const char s_khookCommit[] = SOURCE2MOD_KHOOK_COMMIT;
        ptr = const_cast<char*>(s_khookCommit);
    }
    else
    {
        size_t count = 0;
        const CurrentInterface* current = CurrentInterfaces(count);

        for (size_t i = 0; i < count && !ptr; i++)
        {
            if (!strcmp(iface, current[i].iface))
                ptr = current[i].ptr;
        }

        // Not the current revision: the same interface at another one? The
        // name carries the revision ("ISource2ModMenus002"); the C++ name
        // never changes, so the plugin asks by the string it was built with.
        // The answer says which side has to move.
        size_t nameLen = 0;
        int wanted = 0;
        if (!ptr && SplitRevision(iface, nameLen, wanted))
        {
            for (size_t i = 0; i < count; i++)
            {
                size_t curLen = 0;
                int have = 0;
                if (!SplitRevision(current[i].iface, curLen, have) || curLen != nameLen || strncmp(iface, current[i].iface, nameLen) != 0)
                    continue;

                if (wanted < have)
                    FP_ERROR("{} was built against {}, this core serves {}: rebuild the plugin against the current SDK", who, iface, current[i].iface);
                else
                    FP_ERROR("{} asks for {}, newer than this core's {}: update the core", who, iface, current[i].iface);
                break;
            }
        }
    }

    if (ptr)
    {
        if (s_pLoadingPlugin)
            s_pLoadingPlugin->ifaces.push_back(iface);
        if (ret) *ret = SOURCE2MOD_IFACE_OK;
        return ptr;
    }

    for (auto& p : pluginManager.m_plugins)
    {
        for (auto* l : p->listeners)
        {
            if (auto res = l->OnSource2ModQuery(iface, ret))
            {
                if (id) *id = p->id;
                return res;
            }
        }
    }

    if (ret) *ret = SOURCE2MOD_IFACE_FAILED;
    return nullptr;
}

void* PluginApi::MetaFactory(const char* iface, int* ret, PluginId* id)
{
    return g_SMAPI->MetaFactory(iface, ret, id);
}

ISource2ModAddresses* PluginApi::Addresses()
{
    return &addresses::source2modAddresses;
}

ISource2ModCommands* PluginApi::Commands()
{
    return &commands::commandsManager;
}

ISource2ModConVars* PluginApi::ConVars()
{
    return &convars::convarsManager;
}

ISource2ModCustomHud* PluginApi::CustomHud()
{
    return &customhud::customHudManager;
}

ISource2ModSounds* PluginApi::Sounds()
{
    return &sounds::soundsManager;
}

ISource2ModGameHooks* PluginApi::GameHooks()
{
    return &gamehooks::gameHooksManager;
}

ISource2ModTransmit* PluginApi::Transmit()
{
    return &transmit::transmitManager;
}

ISource2ModEntities* PluginApi::Entities()
{
    return &entities::entitiesManager;
}

ISource2ModEvents* PluginApi::Events()
{
    return &events::eventManager;
}

ISource2ModGameConfig* PluginApi::GameConfig()
{
    return shared::g_pGameConfig;
}

ISource2ModGameSystems* PluginApi::GameSystems()
{
    return &gamesystems::gameSystemsManager;
}

ISource2ModHTTP* PluginApi::HTTP()
{
    return &http::httpManager;
}

ISource2ModMenus* PluginApi::Menus()
{
    return &menus::menuManager;
}

ISource2ModMySQL* PluginApi::MySQL()
{
    return &mysql::mysqlManager;
}

ISource2ModNetworkMessages* PluginApi::NetworkMessages()
{
    return &networkmessages::networkMessagesManager;
}

ISource2ModScheduler* PluginApi::Scheduler()
{
    return &scheduler::schedulerManager;
}

ISource2ModTrace* PluginApi::Trace()
{
    return &raytrace::rayTrace;
}

ISource2ModPaths* PluginApi::Paths()
{
    return &paths::pathsManager;
}

ISource2ModModule* PluginApi::LoadModule(const char* name)
{
    return new Source2ModModule(name);
}

ISource2ModModule* PluginApi::LoadModuleFromMemory(uintptr_t ptr)
{
    return new Source2ModModule(ptr);
}

void PluginApi::FreeModule(ISource2ModModule* module)
{
    delete module;
}

ISource2Server* PluginApi::GetSource2Server()
{
    return g_pSource2Server;
}

ISource2ServerConfig* PluginApi::GetSource2ServerConfig()
{
    return g_pSource2ServerConfig;
}

ISource2GameClients* PluginApi::GetSource2GameClients()
{
    return g_pSource2GameClients;
}

ISource2GameEntities* PluginApi::GetSource2GameEntities()
{
    return g_pSource2GameEntities;
}

IVEngineServer2* PluginApi::GetEngineServer()
{
    return g_pEngineServer;
}

ICvar* PluginApi::GetCVar()
{
    return g_pCVar;
}

// interfaces.h only knows the interface type; plugins want the concrete class,
// so the downcast happens here rather than in every plugin.
CSchemaSystem* PluginApi::GetSchemaSystem()
{
    return shared::g_pSchemaSystem;
}

IGameResourceService* PluginApi::GetGameResourceService()
{
    return g_pGameResourceServiceServer;
}

INetworkServerService* PluginApi::GetNetworkServerService()
{
    return g_pNetworkServerService;
}

INetworkSystem* PluginApi::GetNetworkSystem()
{
    return g_pNetworkSystem;
}

INetworkMessages* PluginApi::GetNetworkMessages()
{
    return g_pNetworkMessages;
}

INetworkStringTableContainer* PluginApi::GetNetworkStringTableServer()
{
    return g_pNetworkStringTableServer;
}

IEngineServiceMgr* PluginApi::GetEngineServiceMgr()
{
    return g_pEngineServiceMgr;
}

IHostStateMgr* PluginApi::GetHostStateMgr()
{
    return g_pHostStateMgr;
}

ILocalize* PluginApi::GetLocalize()
{
    return g_pLocalize;
}

IFileSystem* PluginApi::GetFileSystem()
{
    return g_pFullFileSystem;
}

IGameEventSystem* PluginApi::GetGameEventSystem()
{
    return shared::g_pGameEventSystem;
}

IGameEventManager2* PluginApi::GetGameEventManager()
{
    return shared::g_pGameEventManager;
}

CGameEntitySystem* PluginApi::GetEntitySystem()
{
    return shared::g_pEntitySystem;
}

CGlobalVars* PluginApi::GetGlobalVars()
{
    return shared::getGlobalVars();
}

const char* PluginApi::GetBaseDir()
{
    return g_SMAPI->GetBaseDir();
}

size_t PluginApi::Format(char* buffer, size_t maxlength, const char* format, ...)
{
    va_list ap;
    size_t result;

    va_start(ap, format);
    result = FormatArgs(buffer, maxlength, format, ap);
    va_end(ap);

    return result;
}

size_t PluginApi::FormatArgs(char* buffer, size_t maxlength, const char* format, va_list ap)
{
    return g_SMAPI->FormatArgs(buffer, maxlength, format, ap);
}
