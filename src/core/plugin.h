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

#ifndef _INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_
#define _INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_

#include "ISmmPlugin.h"

// ISmmPlugin.h brings KHook, metamod's detour library: PLUGIN_SAVEVARS() fetches
// the engine, Source2Mod hooks with it and hands the same engine to its plugins
// through SOURCE2MOD_KHOOK_INTERFACE (pluginapi.cpp).

class Source2ModCore : public ISmmPlugin, public IMetamodListener
{
public:
    bool Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late) override;
    bool Unload(char *error, size_t maxlen) override;
    void AllPluginsLoaded() override;

    /// Answers an interface query from a plain metamod plugin by asking the
    /// source2mod's own plugins. Without this the bridge is one-way: a source2mod
    /// plugin can reach a metamod one through MetaFactory, but not the reverse.
    void* OnMetamodQuery(const char* iface, int* ret) override;
public:
    void OnPluginLoad(PluginId id) override;
    void OnPluginUnload(PluginId id) override;
    void OnLevelInit(char const* pMapName, char const* pMapEntities, char const* pOldLevel, char const* pLandmarkName, bool loadGame, bool background) override;
    void OnLevelShutdown() override;
public:
    const char *GetAuthor() override;
    const char *GetName() override;
    const char *GetDescription() override;
    const char *GetURL() override;
    const char *GetLicense() override;
    const char *GetVersion() override;
    const char *GetDate() override;
    const char *GetLogTag() override;
};

extern Source2ModCore g_Source2ModCore;

#endif //_INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_

PLUGIN_GLOBALVARS();
