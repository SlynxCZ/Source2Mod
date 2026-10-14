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
#include "source2mod/schema/entityio.h"

// KHook, via metamod.
#include "ISmmPlugin.h"
#include "engine2/isource2server.h"
#include "engine2/ivengineserver2.h"
#include "entitysystem.h"
#include "networksystem/inetworksystem.h"

#include "dynlibutils/memaddr.hpp"

namespace inlinehooks {
    // Functions hooked at an address a signature scan found, not through a
    // vtable -- KHook::Member, since they are member functions.
    class Inlines {
    public:
        Inlines();

        void InitListeners();
        void DestructListeners();
    public:
        KHook::Return<void> Hook_FireOutputInternal(CEntityIOOutput* pThis, CEntityInstance* pActivator, CEntityInstance* pCaller, void* variantValue, float delay, void* unk01, void* unk02);
    protected:
        // KHook hooks only come down in their destructor, so they live behind
        // plain pointers: new in the constructor, delete in DestructListeners().
        KHook::Member<CEntityIOOutput, void, CEntityInstance*, CEntityInstance*, void*, float, void*, void*>* m_hFireOutputInternal = nullptr;
    };

    extern Inlines inlines;
    extern std::unordered_map<OutputKey, EntityIOCallbackPair, OutputKeyHash> entityIOListenerStack;
}
