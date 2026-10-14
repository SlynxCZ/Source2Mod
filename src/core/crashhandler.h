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

/// Crash handler: Google Breakpad minidumps with local symbolization and a
/// Discord webhook report.
///
/// A port of AcceleratorLocal (asherkin's accelerator by way of komashchenko's
/// fork) into the core. When the server dies on a signal a minidump plus a
/// .txt with the map, game path, command line and console history is written
/// to addons/source2mod/dumps/. The next start finds the unprocessed dump,
/// walks its stack, symbolizes every addons/ module in-process straight from
/// the ELF/DWARF on disk -- so no llvm-symbolizer or addr2line is needed
/// inside a bare steamrt container -- prints the stack with the suspected
/// culprit and, when CrashDiscordWebhook is set in the core config, posts it
/// to Discord with the .txt attached.
///
/// Linux only; every function below is a no-op on Windows.
namespace crashhandler
{
    /// Installs the exception handler and processes whatever the previous
    /// session left behind. Reads CrashHandlerEnabled and CrashDiscordWebhook
    /// from the core config, so it has to run after that is loaded -- and as
    /// early as possible after it, so a crash while plugins load is caught.
    void Init();

    /// Removes the exception handler.
    void Shutdown();

    /// Something else can install its own signal handlers at any time (the
    /// engine does on startup). Checks every frame and puts breakpad's back.
    void OnGameFrame();

    /// The map goes into the crash metadata, and reaching StartupServer is
    /// what marks a session healthy for the crash-loop protection: a server
    /// that crashes twice in a row before ever getting here leaves its dump
    /// unprocessed, so the reporting itself can never keep it down.
    void OnStartupServer(const char* pszMapName);

    /// Steam's HTTP client is there now -- sends the report that waited for
    /// it. The crash is processed during Load(), long before that.
    void OnSteamAPIActivated();
}
