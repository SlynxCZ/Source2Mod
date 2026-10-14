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

#include <sp_vm_api.h>

#include <cstddef>
#include <string>

namespace sp
{
    class BaseRuntime;
}

class ISource2ModNatives;

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

    /**
     * SourceMod's format: the string at params[fmtParam], its arguments right
     * after it (by reference, as `any ...` passes them). %d %i %u %b %f %x %X
     * %s %c %N %L %%, with -, 0, width and precision. Reports a script error
     * and returns false on a bad format or a missing argument.
     */
    bool FormatParams(SourcePawn::IPluginContext* ctx, const cell_t* params, int fmtParam, char* out, size_t maxlen, size_t* written = nullptr);

    /// A string argument; "" when the address is bad.
    const char* LocalString(SourcePawn::IPluginContext* ctx, cell_t addr);

    // Per-area cleanup when a plugin goes (RemoveAllForPlugin calls these).
    void ForgetEventHooks(PluginId id);     // natives.cpp
    void ForgetConVarHooks(PluginId id);    // natives_convars.cpp
    void ForgetEntityListeners(PluginId id); // natives_entities.cpp
    void ForgetGameHooks(PluginId id);       // natives_gamehooks.cpp

    /// ISource2ModNatives001, for native plugins (pluginsys.cpp).
    ISource2ModNatives* NativesInterface();

    /// The ConVar handle for an access index, made on first use.
    cell_t ConVarHandle(uint16_t accessIndex);

    namespace pluginsys
    {
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
