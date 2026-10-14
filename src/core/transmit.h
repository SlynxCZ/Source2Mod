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
#include "hookid.h"

#include <bitvec.h>
#include <const.h>
#include <engine2/isource2server.h>
#include <entity2/entityidentity.h>
#include <entityhandle.h>
#include <playerslot.h>
#include <source2mod/ISource2ModTransmit.h>
#include <utlvector.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace transmit
{
    /// s2sdk's CCheckTransmitInfo (checktransmitinfo.h) is the engine's full
    /// layout; checked here so a change there shows up at compile time.
    static_assert(sizeof(CCheckTransmitInfo) == 584, "CCheckTransmitInfo layout");

    /* =========================
    The view a hook handler gets
    ========================= */

    class TransmitInfoView : public ISource2ModTransmitInfo
    {
    public:
        CPlayerSlot GetPlayerSlot() const override { return m_pInfo->m_nPlayerSlot; }
        bool IsFullUpdate() const override { return m_pInfo->m_bFullUpdate; }

        bool IsTransmitting(int entityIndex) const override;
        void BlockTransmit(int entityIndex) override;
        void BlockTransmit(CEntityInstance* entity) override;
        void AllowTransmit(int entityIndex) override;
        void AllowTransmit(CEntityInstance* entity) override;

        CBitVec<MAX_EDICTS>* GetTransmitEntity() override { return m_pInfo->m_pTransmitEntity; }
        CBitVec<MAX_EDICTS>* GetTransmitNonPlayers() override { return m_pInfo->m_pNonTransmitEntity; }
        CBitVec<MAX_EDICTS>* GetTransmitOutOfPVS() override { return m_pInfo->m_pTransmitOutOfPVS; }
        CBitVec<MAX_EDICTS>* GetTransmitAlways() override { return m_pInfo->m_pTransmitAlways; }

        CCheckTransmitInfo* m_pInfo = nullptr;
    };

    /* =========================
    Per-entity state
    ========================= */

    struct EntityHook
    {
        PluginId owner = 0;
        CEntityHandle handle;
        int ownerEntity = -1;          // entity index, -1 = none
        bool defaultVisible = true;
        bool blockAll = false;

        /// Per viewer slot: the channels hiding the entity (bit N = channel N).
        std::array<uint8_t, ABSOLUTE_PLAYER_LIMIT> hidden{};
        /// Per viewer slot: allowed although hidden by default.
        std::array<bool, ABSOLUTE_PLAYER_LIMIT> shown{};

        bool IsVisible(int slot) const
        {
            return !blockAll && hidden[slot] == 0 && (defaultVisible || shown[slot]);
        }

        void SetVisible(int slot, bool visible, int channel);
        void Reset(int slot)
        {
            hidden[slot] = 0;
            shown[slot] = false;
        }
    };

    /* =========================
    Manager
    ========================= */

    class TransmitManager : public ISource2ModTransmit
    {
    public:
        bool HookEntity(PluginId owner, CEntityInstance* entity, bool defaultVisible) override;
        void UnhookEntity(CEntityInstance* entity) override;
        bool IsEntityHooked(CEntityInstance* entity) override;

        bool SetVisible(CEntityInstance* entity, CPlayerSlot viewer, bool visible, int channel) override;
        bool SetVisibleToAll(CEntityInstance* entity, bool visible, int channel) override;
        bool IsVisible(CEntityInstance* entity, CPlayerSlot viewer) override;
        bool IsHiddenOnChannel(CEntityInstance* entity, CPlayerSlot viewer, int channel) override;

        bool SetBlockAll(CEntityInstance* entity, bool block) override;
        bool GetBlockAll(CEntityInstance* entity) override;

        bool SetOwner(CEntityInstance* entity, CEntityInstance* owner) override;
        CEntityInstance* GetOwner(CEntityInstance* entity) override;

        void ResetViewer(CPlayerSlot viewer) override;


        Source2ModHookId HookCheckTransmit(CheckTransmitHook handler) override { const PluginId owner = hookid::OwnerOfHandler(handler); return HookCheckTransmit(owner, std::move(handler)); }
        bool UnhookCheckTransmit(const CheckTransmitHook& handler) override { return UnhookCheckTransmit(hookid::OwnerOfHandler(handler), handler); }
        bool UnhookCheckTransmit(Source2ModHookId id) override { return UnhookCheckTransmitId(id); }

        // The same with the owner spelled out: what the calls above resolve
        // to, and what the core itself calls.
        Source2ModHookId HookCheckTransmit(PluginId owner, CheckTransmitHook handler);
        bool UnhookCheckTransmit(PluginId owner, const CheckTransmitHook& handler);
        bool UnhookCheckTransmitId(Source2ModHookId id);

    public:
        /// Post hook of ISource2GameEntities::CheckTransmit: the engine has
        /// filled the sets, this takes the hidden entities back out and then
        /// runs the plugins' handlers.
        void OnCheckTransmit(CCheckTransmitInfo** ppInfoList, int infoCount, const uint16_t* pEntityIndices, int nEntities);

        void OnClientDisconnect(CPlayerSlot slot);
        void OnEntityDeleted(CEntityInstance* entity);

        void RemoveAllForPlugin(PluginId id);
        void Clear();

    private:
        /// The hook of a live entity, nullptr otherwise. A stale hook (the
        /// index was reused before OnEntityDeleted got to it) is dropped here.
        EntityHook* Find(CEntityInstance* entity);
        EntityHook* FindByIndex(int index);


        std::array<std::unique_ptr<EntityHook>, MAX_EDICTS> m_hooks{};
        hookid::HookList<CheckTransmitHook> m_checkHooks;

        // One view per viewer, filled in per call -- nothing allocated per tick.
        std::array<TransmitInfoView, ABSOLUTE_PLAYER_LIMIT> m_views{};
        std::array<ISource2ModTransmitInfo*, ABSOLUTE_PLAYER_LIMIT> m_viewPtrs{};
    };

    extern TransmitManager transmitManager;
}
