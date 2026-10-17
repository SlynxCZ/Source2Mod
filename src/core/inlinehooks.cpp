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
#include "inlinehooks.h"

#include "entities.h"

#include "addresses.h"
#include "commands.h"
#include "core/scheduler.h"
#include "events.h"
#include "hooks.h"
#include "networkmessages.h"
#include "plugin.h"
#include "shared.h"
#include "tkvprof.h"

#include <dynlibutils/module.hpp>
#include <engine2/inetworkserverservice.h>
#include <engine2/networkgameserverbase.h>
#include <engine2/serversideclientbase.h>
#include <source2mod/utils/plat.h>

#include <unordered_set>

namespace inlinehooks
{
    Inlines inlines;

    Inlines::Inlines() :
        // void CEntityIOOutput::FireOutputInternal(CEntityInstance* pActivator, CEntityInstance* pCaller, void* variantValue, float delay, void* unk01, void* unk02)
        KHOOK_NEW(m_hFireOutputInternal, this, &Inlines::Hook_FireOutputInternal, nullptr)
    {
    }

    void Inlines::InitListeners()
    {
        // Configure() is what places the detour; an address the scan did not
        // find leaves that hook dormant rather than patching address zero.
        if (void* pFireOutputInternal = addresses::source2modAddresses.FireOutputInternal.GetPtr())
            m_hFireOutputInternal->Configure(pFireOutputInternal);
    }

    void Inlines::DestructListeners()
    {
        delete m_hFireOutputInternal;
        m_hFireOutputInternal = nullptr;
    }

    KHook::Return<void> Inlines::Hook_FireOutputInternal(CEntityIOOutput* pThis, CEntityInstance* pActivator, CEntityInstance* pCaller, void* variantValue, float delay, void* unk01, void* unk02)
    {
        TK_VPROF("Source2Mod::FireOutputInternal");

        const char* outputName = pThis->m_pDesc ? pThis->m_pDesc->m_pName : "";

        // ISource2ModEntities' output listeners: pre may Supersede (the output
        // does not fire), post sees it fired.
        if (!entities::entitiesManager.HasOutputListeners())
            return { KHook::Action::Ignore };

        if (entities::entitiesManager.NotifyOutput(pCaller, outputName, pActivator, delay, false) == Action::Supersede)
            return { KHook::Action::Supersede };

        // Run the rest of the hook chain and the original now, so the post
        // calls below see the output fired. Unlike CallOriginal this leaves
        // the original skipped when another hook on it superseded. Only the
        // type of the member pointer matters, KHook recalls through the detour.
        using FireOutputInternal_t = void (CEntityIOOutput::*)(CEntityInstance*, CEntityInstance*, void*, float, void*, void*);
        auto result = KHook::Recall(static_cast<FireOutputInternal_t>(nullptr), KHook::Return<void>{ KHook::Action::Ignore },
                                    pThis, pActivator, pCaller, variantValue, delay, unk01, unk02);

        entities::entitiesManager.NotifyOutput(pCaller, outputName, pActivator, delay, true);

        return result;
    }
}
