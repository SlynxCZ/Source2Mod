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

#include <sp_vm_api.h>

#include <cstddef>

namespace sourcepawn
{
    /// What MaxClients holds for every script: CS2's slot count.
    constexpr cell_t kMaxClients = 64;

    // enum Action (core.inc)
    constexpr cell_t kPluginContinue = 0;
    constexpr cell_t kPluginChanged = 1;
    constexpr cell_t kPluginHandled = 3;
    constexpr cell_t kPluginStop = 4;

    /// Every native the core binds, terminated by { nullptr, nullptr }.
    const sp_nativeinfo_t* GetCoreNatives();

    /**
     * SourceMod's format: the string at params[fmtParam], its arguments right
     * after it (by reference, as `any ...` passes them). %d %i %u %b %f %x %X
     * %s %c %N %L %%, with -, 0, width and precision. Reports a script error
     * and returns false on a bad format or a missing argument.
     */
    bool FormatParams(SourcePawn::IPluginContext* ctx, const cell_t* params, int fmtParam, char* out, size_t maxlen, size_t* written = nullptr);
}
