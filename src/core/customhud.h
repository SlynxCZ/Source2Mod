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
#include "hookid.h"

#include <ehandle.h>
#include <source2mod/ISource2ModCustomHud.h>

#include <vector>

class CCSCustomHudLayout;
class CCSPlayerController;

namespace customhud {
    struct ClickCallbackEntry
    {
        PluginId owner;
        CHandle<CCSCustomHudLayout> layout;
        CustomHudClickHandler handler;
        Source2ModHookId id;
    };

    class CustomHudManager : public ISource2ModCustomHud {
    public:

        Source2ModCustomHudHookId HookCustomHudClick(CCSCustomHudLayout* pLayout, CustomHudClickHandler handler) override { const PluginId owner = hookid::OwnerOfHandler(handler); return HookCustomHudClick(owner, pLayout, std::move(handler)); }
        bool UnhookCustomHudClick(CCSCustomHudLayout* pLayout, const CustomHudClickHandler& handler) override { return UnhookCustomHudClick(hookid::OwnerOfHandler(handler), pLayout, handler); }
        bool UnhookCustomHudClick(Source2ModCustomHudHookId id) override { return UnhookCustomHudClickId(id); }

        // The same with the owner spelled out: what the calls above resolve
        // to, and what the core itself calls.
        Source2ModCustomHudHookId HookCustomHudClick(PluginId owner, CCSCustomHudLayout* pLayout, CustomHudClickHandler handler);
        bool UnhookCustomHudClick(PluginId owner, CCSCustomHudLayout* pLayout, const CustomHudClickHandler& handler);
        bool UnhookCustomHudClickId(Source2ModHookId id);
        void UnhookCustomHudClick(CCSCustomHudLayout* pLayout) override;
    public:
        /// Routes one CS_UM_CustomHudClicked payload to the callbacks of the
        /// layout it names. Stale entries are dropped on the way through.
        void HandleClick(CCSPlayerController* pController, const void* pBuffer, uint32 nSize);

        /// A callback outlives neither its plugin nor the entity it points at:
        /// the handler is a std::function holding code inside the plugin's
        /// library, so it has to go before that library is closed.
        void RemoveAllForPlugin(PluginId id);
        void Clear();
    protected:
        std::vector<ClickCallbackEntry> m_callbacks;
    };

    extern CustomHudManager customHudManager;
}
