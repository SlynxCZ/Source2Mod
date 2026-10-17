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
 * The core's SourcePawn natives: the SourceMod API (as declared by the include
 * set in addons/source2mod/scripting/include) on Source2Mod's own systems.
 */
#pragma once

#include <source2mod/ISource2ModTypes.h>
#include <source2mod/ISource2ModFunctions.h>

#include <entityhandle.h>

#include <sp_vm_api.h>

#include <cstddef>
#include <string>
#include <vector>

namespace sp
{
    class BaseRuntime;
}

class CEntityInstance;
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
    const sp_nativeinfo_t* GetRawHookNatives();      // natives_rawhooks.cpp: HookFunction, FunctionContext

    /// The core function behind a Source2ModFunction handle (natives_gameconfig.cpp).
    ISource2ModFunction* DescribedFunction(SourcePawn::IPluginContext* ctx, cell_t handle);

    /// Drops the function hooks a script made (natives_rawhooks.cpp).
    void ForgetFunctionHooks(PluginId id);
    void ForgetEntityListeners(PluginId id);
    bool UnhookFunctionHook(PluginId owner, Source2ModHookId id);
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

    /**
     * Entities on the script side (natives_entities.cpp): every entity
     * methodmap value is an entity handle -- index and serial -- so a value
     * kept after its entity is removed reads as gone instead of naming
     * whatever reuses the index. -1 is NULL_ENTITY (and CEntityHandle's own
     * invalid value). EntityCell(handle) does not check the entity is alive.
     */
    cell_t EntityCell(CEntityInstance* inst);
    cell_t EntityCell(CEntityHandle handle);
    /// Whether `value` has an entity cell's shape (NULL_ENTITY does not); says nothing about the entity being alive.
    bool IsEntityCell(cell_t value);
    /// The entity index an entity cell carries, alive or not; -1 for NULL_ENTITY and anything else.
    cell_t EntityCellIndex(cell_t value);
    /// The live entity an entity cell names; nullptr for NULL_ENTITY, a removed entity, or anything else.
    CEntityInstance* EntityFromCell(cell_t value);
    template <typename T>
    T* EntityFromCell(cell_t value)
    {
        return static_cast<T*>(EntityFromCell(value));
    }
    /// An entity argument: the live entity, or nullptr after a script error (null, not an entity, removed).
    CEntityInstance* EntityArg(SourcePawn::IPluginContext* ctx, cell_t value);
    /// An argument that may be NULL_ENTITY (nullptr, *ok true); *ok is false after a script error.
    CEntityInstance* OptionalEntityArg(SourcePawn::IPluginContext* ctx, cell_t value, bool* ok);
    /// The handle of the live entity a cell names; an empty one for NULL_ENTITY or a removed entity.
    CEntityHandle HandleFromCell(cell_t value);

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
