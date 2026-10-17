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
#include "pluginmanager.h"

#include "commands.h"
#include "convars.h"
#include "cookies.h"
#include "core/hooks.h"
#include "core/http.h"
#include "core/menus.h"
#include "core/translations.h"
#include "core/targeting.h"
#include "core/mysql.h"
#include "core/networkmessages.h"
#include "core/permissions.h"
#include "core/plugin.h"
#include "core/scheduler.h"
#include "core/shared.h"
#include "customhud.h"
#include "entities.h"
#include "events.h"
#include "pluginapi.h"
#include "scripts.h"
#include "slowguard.h"
#include "sounds.h"
#include "transmit.h"
#include "functions.h"
#include "sourcepawn/scriptplugin.h"
#include "utils/log.h"
#include "utils/paths.h"

#include <source2mod/ISource2ModApi.h>
#include <source2mod/ISource2ModPlugin.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <unordered_map>

// The file watch: inotify here, ReadDirectoryChangesW (Windows.h, via
// pluginmanager.h) on Windows. networkmessages.h above is not platform
// specific, every unload path below calls into it.
#ifndef _WIN32
#include <sys/inotify.h>
#include <sys/select.h>
#include <unistd.h>
#endif

#include "hookid.h"

PluginManager pluginManager;

// The base of the module an address lies in, or nullptr.
static const void* ModuleBaseOf(const void* address)
{
    if (!address)
        return nullptr;

#ifdef _WIN32
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCSTR>(address), &module))
        return nullptr;
    return module;
#else
    Dl_info info{};
    if (!dladdr(address, &info))
        return nullptr;
    return info.dli_fbase;
#endif
}

PluginId hookid::OwnerOf(const void* address)
{
    return pluginManager.OwnerOf(address);
}

int PluginManager::OwnerOf(const void* address)
{
    if (!address)
        return 0;

    // The plugin list changed since the cache was filled: every answer may be stale.
    if (m_ownerCacheGeneration != m_listGeneration)
    {
        m_ownerCache.clear();
        m_ownerCacheGeneration = m_listGeneration;
    }

    if (auto it = m_ownerCache.find(address); it != m_ownerCache.end())
        return it->second;

    PluginId owner = 0;
    if (const void* base = ModuleBaseOf(address))
    {
        for (const auto& p : m_plugins)
        {
            if (p && p->moduleBase == base)
            {
                owner = p->id;
                break;
            }
        }
    }

    m_ownerCache.emplace(address, owner);
    return owner;
}

static LibHandle OpenLib(const char* path, std::string& outError)
{
#ifdef _WIN32
    auto lib = LoadLibraryA(path);
    if (!lib)
    {
        outError = "LoadLibrary failed";
    }
    return lib;
#else
    dlerror();

    void* lib = dlopen(path, RTLD_NOW);
    if (!lib)
    {
        const char* err = dlerror();
        outError = err ? err : "unknown dlopen error";
    }

    return lib;
#endif
}

static void CloseLib(LibHandle lib)
{
#ifdef _WIN32
    // Loaded from a private copy (ShadowCopy): it goes with the library.
    char path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameA(lib, path, sizeof(path));
    FreeLibrary(lib);

    if (length > 0 && length < sizeof(path))
    {
        std::error_code ec;
        const std::filesystem::path file(path);
        if (std::filesystem::equivalent(file.parent_path(), paths::GetPluginCacheDirectory(), ec))
            std::filesystem::remove(file, ec);
    }
#else
    dlclose(lib);
#endif
}

// A native plugin is loaded from a private copy of its file, never from the
// file in plugins/ itself. Copying a new build over a loaded library (cp keeps
// the inode) changed the code of the plugin still running until its hot reload
// came round: a hook called in between ran the new build's bytes at the old
// addresses and crashed. On Windows the copy is also what lets plugins/ be
// written to while the plugin is loaded. Linux unlinks the copy right after
// dlopen (the mapping stays); Windows deletes it when the library is freed.
static std::string ShadowCopy(const char* fullPath, std::string& outError)
{
    namespace fs = std::filesystem;
    std::error_code ec;

    const fs::path dir = paths::GetPluginCacheDirectory();

    // Whatever an earlier run left behind (a crash before FreeLibrary); a copy
    // another server still has loaded cannot be deleted and stays.
    static const bool s_cleaned = [&dir] {
        std::error_code e;
        for (const auto& entry : fs::directory_iterator(dir, e))
            fs::remove(entry.path(), e);
        return true;
    }();
    (void)s_cleaned;

    fs::create_directories(dir, ec);

#ifdef _WIN32
    const unsigned long pid = GetCurrentProcessId();
#else
    const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
    static unsigned s_counter = 0;

    const fs::path source(fullPath);
    const fs::path copy = dir / (source.stem().string() + "." + std::to_string(pid) + "." + std::to_string(++s_counter) + source.extension().string());

    if (!fs::copy_file(source, copy, fs::copy_options::overwrite_existing, ec))
    {
        outError = ec.message();
        return {};
    }

    return copy.string();
}

static void* GetSymbol(LibHandle lib, const char* name)
{
#ifdef _WIN32
    return (void*)GetProcAddress(lib, name);
#else
    return dlsym(lib, name);
#endif
}

static std::string FindPluginBinary(const std::string& name)
{
    namespace fs = std::filesystem;

    auto dir = paths::GetPluginsDirectory();

    if (!fs::exists(dir) || !fs::is_directory(dir))
        return "";

    for (const auto& entry : fs::directory_iterator(dir))
    {
        if (!entry.is_regular_file())
            continue;

        auto path = entry.path();

        if (path.extension() != ".smx")
            continue;

        if (path.stem().string().find(name) != std::string::npos)
        {
            return path.string();
        }
    }

    return "";
}

#define FAIL(msg) \
    do { \
        if (error && maxlen) snprintf(error, maxlen, "%s", msg); \
        if (lib) CloseLib(lib); \
        return false; \
    } while(0)

#define FAILF(fmt, ...) \
    do { \
        if (error && maxlen) snprintf(error, maxlen, fmt, __VA_ARGS__); \
        if (lib) CloseLib(lib); \
        return false; \
    } while(0)

bool PluginManager::IsPluginLoaded(const std::string& fullPath)
{
    auto normalized = std::filesystem::weakly_canonical(fullPath).string();

    for (auto& p : m_plugins)
    {
        auto existing = std::filesystem::weakly_canonical(p->path).string();

        if (existing == normalized)
            return true;
    }

    return false;
}

bool PluginManager::LoadPluginFromPath(const char* fullPath, char* error, size_t maxlen, bool hotReload)
{
    // One extension for both kinds: a SourcePawn binary starts with the SMX
    // magic, anything else is a native plugin and goes to the loader.
    if (sourcepawn::IsScriptFile(fullPath))
        return LoadScriptFromPath(fullPath, error, maxlen, hotReload);

    std::string dlErr;
    std::string copyErr;
    const std::string loadPath = ShadowCopy(fullPath, copyErr);
    if (loadPath.empty())
        FP_WARN("No private copy of {} ({}); loading it in place, so it must not be overwritten while loaded", fullPath, copyErr);

    auto lib = OpenLib(loadPath.empty() ? fullPath : loadPath.c_str(), dlErr);

    if (!loadPath.empty())
    {
#ifdef _WIN32
        if (!lib)
#endif
        {
            std::error_code ec;
            std::filesystem::remove(loadPath, ec);
        }
    }

    if (!lib)
    {
        if (error && maxlen)
            snprintf(error, maxlen, "%s", dlErr.c_str());

        FP_ERROR("Failed to load {}: {}", fullPath, dlErr);
        return false;
    }

    auto fn = (CreateInterfaceFn)GetSymbol(lib, "CreateInterface");
    if (!fn)
    {
        FP_ERROR("Failed to load {}: no CreateInterface export", fullPath);
        FAILF("CreateInterface not found in %s", fullPath);
    }

    int ret = 0;
    auto plugin = (ISource2ModPlugin*)fn(SOURCE2MOD_PLAPI_NAME, &ret);

    if (!plugin || ret != SOURCE2MOD_IFACE_OK)
    {
        FP_ERROR("Failed to load {}: it does not expose " SOURCE2MOD_PLAPI_NAME, fullPath);
        FAIL("Invalid plugin interface");
    }

    return AddPlugin(fullPath, lib, ModuleBaseOf(reinterpret_cast<const void*>(fn)), plugin, nullptr, error, maxlen, hotReload);
}

bool PluginManager::LoadScriptFromPath(const char* fullPath, char* error, size_t maxlen, bool hotReload)
{
    std::string err;
    auto script = sourcepawn::Open(fullPath, err);
    if (!script)
    {
        FP_ERROR("Failed to load {}: {}", fullPath, err);
        if (error && maxlen)
            snprintf(error, maxlen, "%s", err.c_str());
        return false;
    }

    ISource2ModPlugin* plugin = script.get();
    return AddPlugin(fullPath, nullptr, nullptr, plugin, std::move(script), error, maxlen, hotReload);
}

bool PluginManager::AddPlugin(const char* fullPath, LibHandle lib, const void* moduleBase, ISource2ModPlugin* plugin,
                              std::shared_ptr<sourcepawn::ScriptPlugin> script, char* error, size_t maxlen, bool hotReload)
{

    // Newer would ask this core for things it does not have.
    if (plugin->GetApiVersion() > SOURCE2MOD_PLAPI_VERSION)
    {
        FP_ERROR("Failed to load {}: plugin API version {} but this core speaks {}", fullPath, plugin->GetApiVersion(), SOURCE2MOD_PLAPI_VERSION);
        FAIL("Plugin built against a newer SDK than this core");
    }

    // Older is refused too since API 3: ISource2ModListener's client callbacks
    // were reordered and ISource2ModCommands went to 003, so an older plugin
    // would be called through the wrong vtable slots and handed no commands
    // interface. Its GetApiVersion() is the first slot and safe to ask.
    if (plugin->GetApiVersion() < SOURCE2MOD_PLAPI_VERSION)
    {
        FP_ERROR("Failed to load {}: plugin API version {} but this core needs {} -- update the plugin or rebuild it against the current SDK",
                 fullPath, plugin->GetApiVersion(), SOURCE2MOD_PLAPI_VERSION);
        FAIL("Plugin built against an older SDK than this core");
    }

    auto pl = std::make_unique<Source2ModPlugin>();
    pl->id = m_nextId++;
    pl->lib = lib;
    pl->moduleBase = moduleBase;
    pl->api = plugin;
    pl->apiVersion = plugin->GetApiVersion();
    pl->path = fullPath;
    pl->script = std::move(script);

    m_plugins.push_back(std::move(pl));
    ++m_listGeneration;

    auto& stored = m_plugins.back();

    // Anything after the startup pass is late: "s2m load", "refresh" and
    // the file watcher's hot reload all land in a server that is already up.
    const bool late = hotReload || m_bStartupLoadDone;

    char err[256]{};
    PluginApi::SetLoadingPlugin(stored.get());
    const bool loaded = plugin->Load(stored->id, &pluginApi, err, sizeof(err), late);
    PluginApi::SetLoadingPlugin(nullptr);

    if (!loaded)
    {
        // The plugin is already in the list -- Load() registers things under
        // its id -- so it has to come out again before the library is closed.
        // Left in, "s2m list" and every listener loop called into a
        // library that was no longer mapped. Whatever Load() managed to
        // register before it gave up goes the same way as on an unload.
        const PluginId failedId = stored->id;
        stored->listeners.clear();

        events::eventManager.RemoveAllForPlugin(failedId);
        commands::commandsManager.RemoveAllForPlugin(failedId);
        customhud::customHudManager.RemoveAllForPlugin(failedId);
        convars::convarsManager.RemoveAllForPlugin(failedId);
        networkmessages::networkMessagesManager.RemoveAllForPlugin(failedId);
        sounds::soundsManager.RemoveAllForPlugin(failedId);
        transmit::transmitManager.RemoveAllForPlugin(failedId);
        scripts::scriptsManager.RemoveAllForPlugin(failedId);
        scheduler::schedulerManager.RemoveAllForPlugin(failedId);
        http::httpManager.RemoveAllForPlugin(failedId);
        mysql::mysqlManager.RemoveAllForPlugin(failedId);
        menus::menuManager.RemoveAllForPlugin(failedId);
        hooks::hooksManager.RemoveAllForPlugin(failedId);
        entities::entitiesManager.RemoveAllForPlugin(failedId);
        permissions::permissionsManager.RemoveAllForPlugin(failedId);
        cookies::cookiesManager.RemoveAllForPlugin(failedId);
        translations::translationsManager.RemoveAllForPlugin(failedId);
        targeting::targetingManager.RemoveAllForPlugin(failedId);
        functions::functionsManager.RemoveAllForPlugin(failedId);
        sourcepawn::RemoveAllForPlugin(failedId);

        m_plugins.pop_back();
        ++m_listGeneration;

        // Said out loud: a plugin that refuses to load is otherwise invisible,
        // LoadAll() has nobody to hand the error to.
        FP_ERROR("Plugin {} refused to load: {}", std::filesystem::path(fullPath).stem().string(), err[0] ? err : "(no reason given)");

        FAILF("Plugin load failed: %s", err);
    }

    auto newId = m_plugins.back()->id;

    for (auto& p : m_plugins)
    {
        for (auto* l : p->listeners)
            l->OnPluginLoad(newId, m_plugins.back()->api->GetName());
    }

    FP_INFO("{} plugin {}", hotReload ? "Hot reloaded" : "Loaded", std::filesystem::path(fullPath).stem().string());
    return true;
}

bool PluginManager::LoadPlugin(const char* name, char* error, size_t maxlen)
{
    std::string fullPath = FindPluginBinary(name);

    if (fullPath.empty())
    {
        if (error && maxlen)
            snprintf(error, maxlen, "Plugin '%s' not found", name);

        return false;
    }

    auto normalized = std::filesystem::weakly_canonical(fullPath).string();

    for (auto& p : m_plugins)
    {
        auto existing = std::filesystem::weakly_canonical(p->path).string();

        if (existing == normalized)
        {
            if (error && maxlen)
                snprintf(error, maxlen, "Plugin already loaded (same file)");
            return false;
        }
    }

    return LoadPluginFromPath(fullPath.c_str(), error, maxlen, false);
}

namespace
{
    // A plugin's hooks are KHook objects living in its own library, and its
    // Unload() deletes them, which takes the detours down synchronously -- so
    // by the time the library is closed nothing in the engine points into it
    // any more. What can still point into it is the stack: "s2m unload" is
    // typed at a console, which Source2Mod reaches from inside a hook of its
    // own, and the plugin may well have been on that path too. The close
    // therefore waits for the next frame, when everything has unwound. A
    // SourcePawn plugin has no library; its runtime is what waits instead.
    void CloseLibNextFrame(const Source2ModPlugin& plugin, std::string reloadPath = {})
    {
        scheduler::schedulerManager.NextFrame(0, [lib = plugin.lib, script = plugin.script, reloadPath = std::move(reloadPath)]() mutable
        {
            if (lib)
                CloseLib(lib);
            script.reset();

            // A reload has to wait for the same moment: dlopen() on a library
            // that is still mapped hands back the same handle without running
            // its static initialisers again, so loading before this point
            // would "reload" the old code.
            if (!reloadPath.empty())
            {
                char err[256]{};
                pluginManager.LoadPluginFromPath(reloadPath.c_str(), err, sizeof(err), true);
            }
        });
    }
}

bool PluginManager::ReloadPlugin(int id)
{
    std::string path;

    for (auto it = m_plugins.begin(); it != m_plugins.end(); ++it)
    {
        if ((*it)->id != id)
            continue;

        path = (*it)->path;

        for (auto& other : m_plugins)
            for (auto* l : other->listeners)
                l->OnPluginUnload(id, (*it)->api->GetName());

        char err[128]{};
        (*it)->api->Unload(err, sizeof(err));
        (*it)->listeners.clear();

        events::eventManager.RemoveAllForPlugin(id);
        commands::commandsManager.RemoveAllForPlugin(id);
        customhud::customHudManager.RemoveAllForPlugin(id);
        convars::convarsManager.RemoveAllForPlugin(id);
        networkmessages::networkMessagesManager.RemoveAllForPlugin(id);
        sounds::soundsManager.RemoveAllForPlugin(id);
        transmit::transmitManager.RemoveAllForPlugin(id);
        scripts::scriptsManager.RemoveAllForPlugin(id);
        // Timers and next-frame tasks run callbacks that live inside the
        // library about to be closed; both the call and destroying the
        // std::function would land in unmapped memory afterwards.
        scheduler::schedulerManager.RemoveAllForPlugin(id);
        http::httpManager.RemoveAllForPlugin(id);
        mysql::mysqlManager.RemoveAllForPlugin(id);
        menus::menuManager.RemoveAllForPlugin(id);
        hooks::hooksManager.RemoveAllForPlugin(id);
        entities::entitiesManager.RemoveAllForPlugin(id);
        permissions::permissionsManager.RemoveAllForPlugin(id);
        cookies::cookiesManager.RemoveAllForPlugin(id);
        translations::translationsManager.RemoveAllForPlugin(id);
        targeting::targetingManager.RemoveAllForPlugin(id);
        functions::functionsManager.RemoveAllForPlugin(id);
        sourcepawn::RemoveAllForPlugin(id);

        CloseLibNextFrame(**it, path);

        m_plugins.erase(it);
        ++m_listGeneration;
        break;
    }

    // The load happens next frame, right after the close; see CloseLibNextFrame().
    return !path.empty();
}

bool PluginManager::ReloadPluginByPath(const std::string& fullPath)
{
    auto normalized = std::filesystem::weakly_canonical(fullPath).string();

    for (auto& p : m_plugins)
    {
        if (std::filesystem::weakly_canonical(p->path).string() == normalized)
            return ReloadPlugin(p->id);
    }

    return false;
}

bool PluginManager::UnloadPlugin(PluginId id)
{
    for (auto it = m_plugins.begin(); it != m_plugins.end(); ++it)
    {
        auto& p = *it;

        if (p->id != id)
            continue;

        // notify listeners FIRST
        for (auto& other : m_plugins)
        {
            for (auto* l : other->listeners)
                l->OnPluginUnload(id, p->api->GetName());
        }

        char err[128]{};
        p->api->Unload(err, sizeof(err));

        p->listeners.clear();

        events::eventManager.RemoveAllForPlugin(id);
        commands::commandsManager.RemoveAllForPlugin(id);
        customhud::customHudManager.RemoveAllForPlugin(id);
        convars::convarsManager.RemoveAllForPlugin(id);
        networkmessages::networkMessagesManager.RemoveAllForPlugin(id);
        sounds::soundsManager.RemoveAllForPlugin(id);
        transmit::transmitManager.RemoveAllForPlugin(id);
        scripts::scriptsManager.RemoveAllForPlugin(id);
        scheduler::schedulerManager.RemoveAllForPlugin(id);
        http::httpManager.RemoveAllForPlugin(id);
        mysql::mysqlManager.RemoveAllForPlugin(id);
        menus::menuManager.RemoveAllForPlugin(id);
        hooks::hooksManager.RemoveAllForPlugin(id);
        entities::entitiesManager.RemoveAllForPlugin(id);
        permissions::permissionsManager.RemoveAllForPlugin(id);
        cookies::cookiesManager.RemoveAllForPlugin(id);
        translations::translationsManager.RemoveAllForPlugin(id);
        targeting::targetingManager.RemoveAllForPlugin(id);
        functions::functionsManager.RemoveAllForPlugin(id);
        sourcepawn::RemoveAllForPlugin(id);

        CloseLibNextFrame(*p);

        m_plugins.erase(it);
        ++m_listGeneration;
        return true;
    }

    return false;
}

bool PluginManager::LoadMissing()
{
    namespace fs = std::filesystem;

    auto dir = paths::GetPluginsDirectory();

    // Nothing to load is not a failure. This used to return false and take the
    // whole source2mod down with it on a fresh install.
    if (!fs::exists(dir) || !fs::is_directory(dir))
        return true;

    char error[256];

    int loaded = 0;

    for (const auto& entry : fs::directory_iterator(dir))
    {
        if (!entry.is_regular_file())
            continue;

        auto path = entry.path();

        if (path.extension() != ".smx")
            continue;

        auto fullPath = path.string();

        if (IsPluginLoaded(fullPath))
            continue;

        if (LoadPlugin(path.stem().string().c_str(), error, sizeof(error)))
        {
            loaded++;
        }
    }

    if (loaded > 0)
        SetAllLoaded();

    return true;
}

bool PluginManager::LoadAll()
{
    namespace fs = std::filesystem;

    if (!shared::g_pCoreConfig->PluginAutoLoadEnabled)
    {
        m_bStartupLoadDone = true;
        return true;
    }

    auto dir = paths::GetPluginsDirectory();

    if (!fs::exists(dir) || !fs::is_directory(dir))
    {
        m_bStartupLoadDone = true;
        SetAllLoaded();
        return true;
    }

    char error[256];
    int failed = 0;

    for (const auto& entry : fs::directory_iterator(dir))
    {
        if (!entry.is_regular_file())
            continue;

        auto path = entry.path();

        if (path.extension() != ".smx")
            continue;

        if (!LoadPlugin(path.stem().string().c_str(), error, sizeof(error)))
            failed++;
    }

    // Each one has said why on its own line; this is the line that gets noticed.
    if (failed)
        FP_WARN("{} plugin(s) did not load, {} running. See the errors above.", failed, m_plugins.size());

    m_bStartupLoadDone = true;
    SetAllLoaded();
    return true;
}

void PluginManager::UnloadAll()
{
    for (auto& p : m_plugins)
    {
        for (auto& other : m_plugins)
        {
            for (auto* l : other->listeners)
                l->OnPluginUnload(p->id, p->api->GetName());
        }

        char err[128]{};
        p->api->Unload(err, sizeof(err));

        p->listeners.clear();

        events::eventManager.RemoveAllForPlugin(p->id);
        commands::commandsManager.RemoveAllForPlugin(p->id);
        customhud::customHudManager.RemoveAllForPlugin(p->id);
        convars::convarsManager.RemoveAllForPlugin(p->id);
        networkmessages::networkMessagesManager.RemoveAllForPlugin(p->id);
        sounds::soundsManager.RemoveAllForPlugin(p->id);
        transmit::transmitManager.RemoveAllForPlugin(p->id);
        scripts::scriptsManager.RemoveAllForPlugin(p->id);
        scheduler::schedulerManager.RemoveAllForPlugin(p->id);
        http::httpManager.RemoveAllForPlugin(p->id);
        mysql::mysqlManager.RemoveAllForPlugin(p->id);
        menus::menuManager.RemoveAllForPlugin(p->id);
        hooks::hooksManager.RemoveAllForPlugin(p->id);
        entities::entitiesManager.RemoveAllForPlugin(p->id);
        permissions::permissionsManager.RemoveAllForPlugin(p->id);
        cookies::cookiesManager.RemoveAllForPlugin(p->id);
        translations::translationsManager.RemoveAllForPlugin(p->id);
        targeting::targetingManager.RemoveAllForPlugin(p->id);
        functions::functionsManager.RemoveAllForPlugin(p->id);
        sourcepawn::RemoveAllForPlugin(p->id);
    }

    // Only once every plugin's registrations are gone. What one plugin owns can
    // hold code from another: a plugin registering a command on behalf of its
    // caller wraps the caller's handler in a lambda of its own, so destroying
    // that entry needs both libraries mapped. Closing each library right after
    // its own cleanup made the outcome depend on directory order -- whichever
    // came first was already unmapped when the other's entries were destroyed.
    //
    // Shutting down: there is no next frame to wait for, and Source2Mod
    // itself is on its way out of every hook, so close right here.
    for (auto& p : m_plugins)
        if (p->lib)
            CloseLib(p->lib);

    m_plugins.clear();
    ++m_listGeneration;
}

// ---- hot reload file watch ----------------------------------------------------

namespace
{
    // How long an .smx has to stay untouched before it is reloaded. A copy or
    // a compile writes in several chunks (and a deploy may rename on top), so
    // every event only pushes the file's deadline back; one reload per file.
    constexpr auto kHotReloadSettle = std::chrono::milliseconds(250);

    bool IsSmxName(const std::string& name)
    {
        return name.size() > 4 && name.compare(name.size() - 4, 4, ".smx") == 0;
    }

    // True once nobody is writing the file any more. On Windows a share mode
    // without FILE_SHARE_WRITE fails while a writer still holds it open; on
    // Linux the open only proves the file is (still) there.
    bool CanOpenForReading(const std::filesystem::path& path)
    {
#ifdef _WIN32
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        CloseHandle(h);
        return true;
#else
        FILE* file = fopen(path.c_str(), "rb");
        if (!file)
            return false;
        fclose(file);
        return true;
#endif
    }

    // Changed .smx files waiting to settle, keyed by file name. Only touched
    // from the watcher thread.
    class HotReloadDebounce
    {
    public:
        void Touch(const std::string& name)
        {
            m_pending[name] = std::chrono::steady_clock::now();
        }

        // Hands every file that has settled to `queue` and forgets it. One
        // that is gone is dropped (deleted or renamed away again), one that
        // cannot be opened yet stays and is tried again next round.
        template <typename F>
        void Flush(const std::filesystem::path& dir, F&& queue)
        {
            const auto now = std::chrono::steady_clock::now();
            for (auto it = m_pending.begin(); it != m_pending.end();)
            {
                if (now - it->second < kHotReloadSettle)
                {
                    ++it;
                    continue;
                }

                const auto path = dir / it->first;
                std::error_code ec;
                if (!std::filesystem::exists(path, ec))
                {
                    it = m_pending.erase(it);
                    continue;
                }

                if (!CanOpenForReading(path))
                {
                    ++it;
                    continue;
                }

                queue(it->first, path);
                it = m_pending.erase(it);
            }
        }

    private:
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_pending;
    };
}

void PluginManager::StartFileWatcher()
{
    if (!shared::g_pCoreConfig->PluginHotReloadEnabled)
        return;

    m_stopWatcher = false;
    auto dir = paths::GetPluginsDirectory();

    m_watcherThread = std::thread([this, dir]()
    {
        const std::filesystem::path dirPath(dir);
        HotReloadDebounce debounce;

        auto queueReload = [this](const std::string& name, const std::filesystem::path& path)
        {
            std::string fullPath = path.string();
            FP_INFO("Detected change in {}, queuing hot reload...", name);
            // Owner 0: Source2Mod's own, so the reload it is
            // about to do does not throw the task away.
            // Off the watcher thread first, then to the command
            // buffer: the reload must not run inside GameFrame.
            scheduler::schedulerManager.NextFrame(0, [this, fullPath]()
            {
                RequestReload(fullPath);
            });
        };

#ifdef _WIN32
        // Overlapped, so the wait below can time out and see m_stopWatcher.
        HANDLE dirHandle = CreateFileW(dirPath.c_str(), FILE_LIST_DIRECTORY,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                       OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (dirHandle == INVALID_HANDLE_VALUE)
        {
            FP_ERROR("Opening {} for change notifications failed ({}), hot reload unavailable", dir, GetLastError());
            return;
        }

        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

        // LAST_WRITE: overwritten in place (copy, compiler output).
        // FILE_NAME: created, or renamed into the folder (move, rsync).
        constexpr DWORD filter = FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME;
        alignas(DWORD) char buf[16384];
        bool armed = false;

        while (!m_stopWatcher)
        {
            if (!armed)
            {
                ResetEvent(overlapped.hEvent);
                if (!ReadDirectoryChangesW(dirHandle, buf, sizeof(buf), FALSE, filter, nullptr, &overlapped, nullptr))
                {
                    FP_ERROR("ReadDirectoryChangesW failed ({}), hot reload stopped", GetLastError());
                    break;
                }
                armed = true;
            }

            if (WaitForSingleObject(overlapped.hEvent, 100) == WAIT_OBJECT_0)
            {
                armed = false;

                DWORD len = 0;
                // 0 bytes: the buffer overflowed and the batch is lost; the
                // next write to the file brings it back.
                if (GetOverlappedResult(dirHandle, &overlapped, &len, FALSE) && len > 0)
                {
                    for (DWORD i = 0;;)
                    {
                        auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buf + i);

                        if (info->Action == FILE_ACTION_ADDED || info->Action == FILE_ACTION_MODIFIED ||
                            info->Action == FILE_ACTION_RENAMED_NEW_NAME)
                        {
                            std::wstring wname(info->FileName, info->FileNameLength / sizeof(WCHAR));
                            std::string name = std::filesystem::path(wname).string();
                            if (IsSmxName(name))
                                debounce.Touch(name);
                        }

                        if (!info->NextEntryOffset)
                            break;
                        i += info->NextEntryOffset;
                    }
                }
            }

            debounce.Flush(dirPath, queueReload);
        }

        if (armed)
        {
            CancelIoEx(dirHandle, &overlapped);
            DWORD len = 0;
            GetOverlappedResult(dirHandle, &overlapped, &len, TRUE);
        }

        CloseHandle(overlapped.hEvent);
        CloseHandle(dirHandle);
#else
        int fd = inotify_init1(IN_NONBLOCK);
        if (fd < 0)
        {
            FP_ERROR("inotify_init1 failed, hot reload unavailable");
            return;
        }

        // CLOSE_WRITE: overwritten in place (cp, compiler output).
        // MOVED_TO: renamed into the folder (mv, rsync's temp file).
        inotify_add_watch(fd, dir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO);

        alignas(struct inotify_event) char buf[4096];

        while (!m_stopWatcher)
        {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(fd, &fds);

            timeval tv{0, 100000};
            if (select(fd + 1, &fds, nullptr, nullptr, &tv) > 0)
            {
                int len = read(fd, buf, sizeof(buf));

                for (int i = 0; i < len;)
                {
                    auto* ev = reinterpret_cast<struct inotify_event*>(buf + i);

                    if (ev->len > 0 && (ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO)))
                    {
                        std::string name = ev->name;
                        if (IsSmxName(name))
                            debounce.Touch(name);
                    }

                    i += static_cast<int>(sizeof(struct inotify_event)) + ev->len;
                }
            }

            debounce.Flush(dirPath, queueReload);
        }

        close(fd);
#endif
    });
}

void PluginManager::StopFileWatcher()
{
    m_stopWatcher = true;
    if (m_watcherThread.joinable())
        m_watcherThread.join();
}

void PluginManager::SetAllLoaded()
{
    for (auto& p : m_plugins)
    {
        for (auto* l : p->listeners)
            l->OnAllSource2ModPluginsLoaded();
    }
}

void PluginManager::FireMetamodLoaded()
{
    for (auto& p : m_plugins)
    {
        for (auto* l : p->listeners)
            l->OnAllMetamodPluginsLoaded();
    }
}

void PluginManager::AddListener(ISource2ModPlugin* plugin, ISource2ModListener* listener)
{
    for (auto& p : m_plugins)
    {
        if (p->api == plugin)
        {
            p->listeners.push_back(listener);
            return;
        }
    }
}

void PluginManager::FireMetamodPluginLoaded(SourceMM::PluginId id)
{
    for (auto& p : pluginManager.m_plugins)
    {
        for (auto* l : p->listeners)
            l->OnMetamodPluginLoad(id);
    }
}

void PluginManager::FireMetamodPluginUnloaded(SourceMM::PluginId id)
{
    for (auto& p : pluginManager.m_plugins)
    {
        for (auto* l : p->listeners)
            l->OnMetamodPluginUnload(id);
    }
}

void PluginManager::OnLevelInit(char const* pMapName, char const* pMapEntities, char const* pOldLevel,
                                char const* pLandmarkName, bool loadGame, bool background)
{
    for (auto& p : pluginManager.m_plugins)
    {
        for (auto* l : p->listeners)
            l->OnLevelInit(pMapName, pMapEntities, pOldLevel, pLandmarkName, loadGame, background);
    }
}

void PluginManager::OnLevelShutdown()
{
    for (auto& p : pluginManager.m_plugins)
    {
        for (auto* l : p->listeners)
            l->OnLevelShutdown();
    }
}

// ---- deferred unload / reload -------------------------------------------------

void PluginManager::QueuePending()
{
    if (m_bPendingQueued)
        return;

    m_bPendingQueued = true;
    g_pEngineServer->ServerCommand("s2m _pending\n");
}

bool PluginManager::RequestUnload(int id)
{
    const bool known = std::any_of(m_plugins.begin(), m_plugins.end(), [id](const auto& p) { return p->id == id; });
    if (known)
    {
        m_unloadRequests.push_back(id);
        QueuePending();
    }
    return known;
}

void PluginManager::RequestReload(const std::string& fullPath)
{
    m_reloadRequests.push_back(fullPath);
    QueuePending();
}

void PluginManager::RunPending()
{
    m_bPendingQueued = false;

    // Taken out first: an unload may unload something else in turn.
    const std::vector<int> unloads = std::move(m_unloadRequests);
    m_unloadRequests.clear();
    const std::vector<std::string> reloads = std::move(m_reloadRequests);
    m_reloadRequests.clear();

    for (const int id : unloads)
        UnloadPlugin(id);

    for (const auto& path : reloads)
        ReloadPluginByPath(path);
}

// ---- engine callbacks (plugin API 2) ----------------------------------------

#define PLUGINS_FANOUT(minVersion, call) \
    for (auto& p : pluginManager.m_plugins) \
    { \
        if (p->apiVersion < (minVersion)) \
            continue; \
        for (auto* l : p->listeners) \
        { \
            ::slow::Guard slowGuard("listener callback", #call, p->id); \
            l->call; \
        } \
    }

const char* PluginManager::NameOf(int id) const
{
    for (const auto& p : m_plugins)
    {
        if (p->id == id && p->api)
            return p->api->GetName();
    }

    return "an unloaded plugin";
}

void PluginManager::OnClientAuthorized(CPlayerSlot slot, uint64 steamId)
{
    PLUGINS_FANOUT(3, OnClientAuthorized(slot, steamId))
}

void PluginManager::OnClientAuthorizeFailed(CPlayerSlot slot, uint64 steamId)
{
    PLUGINS_FANOUT(3, OnClientAuthorizeFailed(slot, steamId))
}

void PluginManager::OnPermissionsChanged(uint64 steamId)
{
    PLUGINS_FANOUT(3, OnPermissionsChanged(steamId))
}

#undef PLUGINS_FANOUT
