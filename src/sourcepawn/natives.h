/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * The core's SourcePawn natives: the SourceMod API (as declared by the include
 * set in addons/source2mod/scripting/include) on Source2Mod's own systems.
 *
 * Project: Source2Mod
 */
#pragma once

#include <source2mod/ISource2ModTypes.h>

#include <entityhandle.h>

#include <sp_vm_api.h>

#include <cstddef>
#include <string>
#include <vector>

namespace sp
{
    class BaseRuntime;
}

class CGameTrace;
class IGameEvent;
class Timer;
class ISource2ModNatives;
class Source2ModCommandArgs;
class Source2ModCommandContext;

namespace sourcepawn
{
    class ScriptPlugin;

    /// What MaxClients holds for every script: CS2's slot count.
    constexpr cell_t kMaxClients = 64;

    // enum Action (core.inc)
    constexpr cell_t kPluginContinue = 0;
    constexpr cell_t kPluginChanged = 1;
    constexpr cell_t kPluginHandled = 3;
    constexpr cell_t kPluginStop = 4;

    /// Every native the core binds, terminated by { nullptr, nullptr }.
    const sp_nativeinfo_t* GetCoreNatives();

    // The tables GetCoreNatives() is made of, one per area.
    const sp_nativeinfo_t* GetAdtNatives();          // natives_adt.cpp
    const sp_nativeinfo_t* GetConVarNatives();       // natives_convars.cpp
    const sp_nativeinfo_t* GetPluginSysNatives();    // pluginsys.cpp
    const sp_nativeinfo_t* GetEntityNatives();       // natives_entities.cpp
    const sp_nativeinfo_t* GetApiNatives();          // natives_api.cpp
    const sp_nativeinfo_t* GetNetSoundNatives();     // natives_netsound.cpp
    const sp_nativeinfo_t* GetGameHookNatives();     // natives_gamehooks.cpp
    const sp_nativeinfo_t* GetEventNatives();        // natives_events.cpp
    const sp_nativeinfo_t* GetGameConfigNatives();   // natives_gameconfig.cpp
    const sp_nativeinfo_t* GetRawHookNatives();      // natives_rawhooks.cpp
    const sp_nativeinfo_t* GetWorldNatives();        // natives_world.cpp
    const sp_nativeinfo_t* GetNetNatives();          // natives_net.cpp
    const sp_nativeinfo_t* GetUiNatives();           // natives_ui.cpp
    const sp_nativeinfo_t* GetMathNatives();         // natives_math.cpp
    const sp_nativeinfo_t* GetFeatureNatives();      // natives_features.cpp
    const sp_nativeinfo_t* GetMapNatives();          // natives_maps.cpp
    const sp_nativeinfo_t* GetGeoIPNatives();        // natives_geoip.cpp
    const sp_nativeinfo_t* GetCookieNatives();       // natives_cookies.cpp
    const sp_nativeinfo_t* GetTranslationNatives();  // natives_translations.cpp
    const sp_nativeinfo_t* GetFileNatives();         // natives_files.cpp
    const sp_nativeinfo_t* GetUtilNatives();         // natives_utils.cpp
    const sp_nativeinfo_t* GetClientNatives();       // natives_clients.cpp
    const sp_nativeinfo_t* GetTargetingNatives();    // natives_targeting.cpp

    /// A float operator native the x64 JIT cannot inline: bound with SP_NTVFLAG_EPHEMERAL.
    bool IsEphemeralMathNative(const char* name);

    /**
     * SourceMod's format: the string at params[fmtParam], its arguments right
     * after it (by reference, as `any ...` passes them). %d %i %u %b %f %x %X
     * %s %c %N %L %%, with -, 0, width and precision. Reports a script error
     * and returns false on a bad format or a missing argument.
     */
    bool FormatParams(SourcePawn::IPluginContext* ctx, const cell_t* params, int fmtParam, char* out, size_t maxlen, size_t* written = nullptr);

    /// Whom Format's %t translates for (a player slot, -1 = the server).
    void SetFormatTarget(int slot);

    /**
     * The translated text of phrase `name` for `target` into `result`, its
     * {n} placeholders formatted from params[arg...] (arg moves past them) by
     * the phrase's #format specs. A phrase not there shows as its name.
     */
    bool TranslateInto(SourcePawn::IPluginContext* ctx, const cell_t* params, int& arg, int target, const char* name, std::string& result, int depth = 0);

    /// A string argument; "" when the address is bad.
    const char* LocalString(SourcePawn::IPluginContext* ctx, cell_t addr);

    // Per-area cleanup when a plugin goes (RemoveAllForPlugin calls these).
    void ForgetEventHooks(PluginId id);     // natives.cpp
    void ForgetConVarHooks(PluginId id);    // natives_convars.cpp
    void ForgetEntityListeners(PluginId id); // natives_entities.cpp
    void ForgetGameHooks(PluginId id);       // natives_gamehooks.cpp
    void ForgetWorldHooks(PluginId id);      // natives_world.cpp
    void ForgetUiHooks(PluginId id);         // natives_ui.cpp
    void ForgetNetSoundHooks(PluginId id);   // natives_netsound.cpp

    /// ISource2ModNatives001, for native plugins (pluginsys.cpp).
    ISource2ModNatives* NativesInterface();

    /// The object behind a SchemaObject handle, nullptr (and a script error) when it is gone.
    void* SchemaObjectPointer(SourcePawn::IPluginContext* ctx, cell_t handle);
    /// A SchemaObject for a raw pointer; 0 for nullptr.
    cell_t SchemaObjectForPointer(void* ptr);

    /// A Source2ModCommandArgs handle for `args`; the caller frees it (Handles().Free) before args goes.
    cell_t LendCommandArgs(const ::Source2ModCommandArgs* args, PluginId owner);

    /// A Source2ModCommandContext handle for `context`; the caller frees it (Handles().Free) before it goes.
    cell_t LendCommandContext(const ::Source2ModCommandContext* context, PluginId owner);

    /// The function behind a Source2ModAddress (addresses.inc); nullptr when not resolved, and a script error for a bad value.
    void* AddressOf(SourcePawn::IPluginContext* ctx, cell_t address);

    /// A TraceResult handle (trace.inc) holding a copy of `trace`, owned by the caller (natives_world.cpp).
    cell_t NewTraceResult(SourcePawn::IPluginContext* ctx, const CGameTrace* trace);

    /// The scheduler's timer behind a timer handle; nullptr once it has ended (or a script error for a bad handle) (natives.cpp).
    Timer* ScriptTimer(SourcePawn::IPluginContext* ctx, cell_t handle);

    /// The event behind an Event handle (hooked or created); nullptr and a script error when it is none (natives.cpp).
    IGameEvent* ScriptEvent(SourcePawn::IPluginContext* ctx, cell_t handle);
    /// A created-event handle (IGameEventManager2.CreateEvent's kind) owning `event`; frees the event on failure.
    cell_t NewScriptEvent(IGameEvent* event, PluginId owner);
    /// Takes the event out of a created-event handle and closes the handle; nullptr (and a script error) for another handle.
    IGameEvent* TakeScriptEvent(SourcePawn::IPluginContext* ctx, cell_t handle);

    /// The message behind a NetMessage handle (a protobuf Message*); nullptr and a script error when empty (natives_netsound.cpp).
    void* NetMessagePointer(SourcePawn::IPluginContext* ctx, cell_t handle);

    /// A new ArrayList (block size 1) holding `items`, owned by `owner` (natives_adt.cpp).
    cell_t NewCellList(const std::vector<cell_t>& items, PluginId owner);

    /**
     * Strings into an ArrayList (natives_adt.cpp): into `handle` when it is
     * one (cleared first with `clear`), else into a new one of `blocksize`
     * cells owned by `owner`. Returns the handle, 0 after a script error.
     */
    cell_t FillStringList(SourcePawn::IPluginContext* ctx, cell_t handle, const std::vector<std::string>& items, bool clear, cell_t blocksize, PluginId owner);

    /// The items of an ArrayList read as strings (natives_adt.cpp); false (and a script error) for a bad handle.
    bool ReadStringList(SourcePawn::IPluginContext* ctx, cell_t handle, std::vector<std::string>& items);

    /// The first cell of each item of an ArrayList (natives_adt.cpp); false (and a script error) for a bad handle.
    bool ReadCellList(SourcePawn::IPluginContext* ctx, cell_t handle, std::vector<cell_t>& items);

    /// A StringMap's string and cell (as decimal) entries; false (and a script error) for a bad handle (natives_adt.cpp).
    bool StringMapStrings(SourcePawn::IPluginContext* ctx, cell_t handle, std::vector<std::pair<std::string, std::string>>& out);

    /// An entity handle as the value CEntityInstance.GetRefEHandle() gives, -1 when empty (natives_entities.cpp).
    cell_t RefForHandle(CEntityHandle handle);
    /// The handle behind a GetRefEHandle() value; an empty one for -1 or a bad value.
    CEntityHandle HandleForRef(cell_t ref);

    /// The ConVar handle for an access index, made on first use.
    cell_t ConVarHandle(uint16_t accessIndex);

    namespace pluginsys
    {
        /// A plugin native (CreateNative / ISource2ModNatives) by name: -1 when
        /// none was ever registered, 0 while its plugin is unloaded, 1 when callable.
        int NativeStatus(const char* name);

        /// Whether some loaded plugin registered the library.
        bool LibraryAvailable(const char* name);

        /// Binds the native at `index` if a plugin registered one by that name.
        bool BindNative(sp::BaseRuntime* runtime, uint32_t index, const char* name);

        /// A script that could not load for want of plugin natives: tried
        /// again when some plugin registers natives.
        void NoteWaiting(const std::string& path);

        /// The plugin finished loading: its libraries are announced.
        void OnPluginLoaded(PluginId id);

        void RemoveAllForPlugin(PluginId id);
    }
}
