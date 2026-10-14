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
#include "transmit.h"

#include "shared.h"
#include "utils/log.h"

#include <entity2/entityinstance.h>
#include <entity2/entitysystem.h>
#include <source2mod/schema/entity/classes/CCSObserverPawn.h>
#include <source2mod/schema/entity/classes/CCSPlayerController.h>
#include <source2mod/schema/entity/classes/CCSPlayerPawn.h>
#include <tier0/tier0_strtools.h>

#include <algorithm>

namespace transmit
{
    TransmitManager transmitManager;

    /* =========================
    Helpers
    ========================= */

    namespace
    {
        bool IsValidIndex(int index)
        {
            return index > 0 && index < MAX_EDICTS;
        }

        bool IsValidSlot(int slot)
        {
            return slot >= 0 && slot < ABSOLUTE_PLAYER_LIMIT;
        }

        int MaxClients()
        {
            CGlobalVars* globals = shared::getGlobalVars();
            // Spelled out: <windows.h> has a min() macro.
            if (!globals)
                return 0;
            return globals->maxClients < ABSOLUTE_PLAYER_LIMIT ? globals->maxClients : ABSOLUTE_PLAYER_LIMIT;
        }

        int IndexOf(CEntityInstance* entity)
        {
            return entity ? entity->GetEntityIndex().Get() : -1;
        }

        /// The player pawns are what a hooked controller stands for; hooking
        /// one directly would only fight that.
        bool IsPawn(CEntityInstance* entity)
        {
            const char* classname = entity->GetClassname();
            return classname && (!V_strcmp(classname, "player") || !V_strcmp(classname, "observer"));
        }

        /// Both bits at once: out of the transmit set, onto the list of
        /// entities the client keeps a record of. Just clearing the first one
        /// makes the client forget the entity, and it then crashes with
        /// "missing client entity" when the entity is sent again.
        void Block(CCheckTransmitInfo* info, int index)
        {
            info->m_pTransmitEntity->Clear(index);
            info->m_pNonTransmitEntity->Set(index);
        }
    }

    /* =========================
    TransmitInfoView
    ========================= */

    bool TransmitInfoView::IsTransmitting(int entityIndex) const
    {
        return IsValidIndex(entityIndex) && m_pInfo->m_pTransmitEntity->IsBitSet(entityIndex);
    }

    void TransmitInfoView::BlockTransmit(int entityIndex)
    {
        if (IsValidIndex(entityIndex))
            Block(m_pInfo, entityIndex);
    }

    void TransmitInfoView::BlockTransmit(CEntityInstance* entity)
    {
        BlockTransmit(IndexOf(entity));
    }

    void TransmitInfoView::AllowTransmit(int entityIndex)
    {
        if (IsValidIndex(entityIndex))
            m_pInfo->m_pTransmitEntity->Set(entityIndex);
    }

    void TransmitInfoView::AllowTransmit(CEntityInstance* entity)
    {
        AllowTransmit(IndexOf(entity));
    }

    /* =========================
    EntityHook
    ========================= */

    void EntityHook::SetVisible(int slot, bool visible, int channel)
    {
        uint8_t mask;
        if (channel == TRANSMIT_CHANNEL_ALL)
            mask = uint8_t((1u << TRANSMIT_MAX_CHANNELS) - 1);
        else
            mask = uint8_t(1u << std::clamp(channel, 0, TRANSMIT_MAX_CHANNELS - 1));

        if (visible)
        {
            hidden[slot] &= uint8_t(~mask);
            shown[slot] = true;
        }
        else
        {
            hidden[slot] |= mask;
        }
    }

    /* =========================
    Lookup
    ========================= */

    EntityHook* TransmitManager::FindByIndex(int index)
    {
        if (!IsValidIndex(index))
            return nullptr;

        EntityHook* hook = m_hooks[index].get();
        if (!hook)
            return nullptr;

        // The entity the hook was made for may be gone and its index handed
        // to another one. Compare the handle rather than trust the index.
        CEntityInstance* live = shared::g_pEntitySystem ? shared::g_pEntitySystem->GetEntityInstance(CEntityIndex(index)) : nullptr;
        if (!live || live->GetRefEHandle() != hook->handle)
        {
            m_hooks[index].reset();
            return nullptr;
        }

        return hook;
    }

    EntityHook* TransmitManager::Find(CEntityInstance* entity)
    {
        if (!entity)
            return nullptr;

        const int index = IndexOf(entity);
        if (!IsValidIndex(index))
            return nullptr;

        EntityHook* hook = m_hooks[index].get();
        if (!hook)
            return nullptr;

        if (hook->handle != entity->GetRefEHandle())
        {
            m_hooks[index].reset();
            return nullptr;
        }

        return hook;
    }

    /* =========================
    Hooking entities
    ========================= */

    bool TransmitManager::HookEntity(PluginId owner, CEntityInstance* entity, bool defaultVisible)
    {
        if (!entity)
            return false;

        const int index = IndexOf(entity);
        if (!IsValidIndex(index))
        {
            FP_WARN("Transmit: entity index {} is out of range", index);
            return false;
        }

        if (IsPawn(entity))
        {
            FP_WARN("Transmit: hook the player's controller, not the pawn ({} at {})", entity->GetClassname(), index);
            return false;
        }

        if (Find(entity))
        {
            FP_WARN("Transmit: {} at {} is already hooked", entity->GetClassname(), index);
            return false;
        }

        auto hook = std::make_unique<EntityHook>();
        hook->owner = owner;
        hook->handle = entity->GetRefEHandle();
        hook->defaultVisible = defaultVisible;
        m_hooks[index] = std::move(hook);
        return true;
    }

    void TransmitManager::UnhookEntity(CEntityInstance* entity)
    {
        if (Find(entity))
            m_hooks[IndexOf(entity)].reset();
    }

    bool TransmitManager::IsEntityHooked(CEntityInstance* entity)
    {
        return Find(entity) != nullptr;
    }

    /* =========================
    Visibility
    ========================= */

    bool TransmitManager::SetVisible(CEntityInstance* entity, CPlayerSlot viewer, bool visible, int channel)
    {
        EntityHook* hook = Find(entity);
        if (!hook || !IsValidSlot(viewer.Get()))
            return false;

        hook->SetVisible(viewer.Get(), visible, channel);
        return true;
    }

    bool TransmitManager::SetVisibleToAll(CEntityInstance* entity, bool visible, int channel)
    {
        EntityHook* hook = Find(entity);
        if (!hook)
            return false;

        for (int slot = 0; slot < ABSOLUTE_PLAYER_LIMIT; slot++)
            hook->SetVisible(slot, visible, channel);

        return true;
    }

    bool TransmitManager::IsVisible(CEntityInstance* entity, CPlayerSlot viewer)
    {
        EntityHook* hook = Find(entity);
        if (!hook)
            return true;

        return IsValidSlot(viewer.Get()) && hook->IsVisible(viewer.Get());
    }

    bool TransmitManager::IsHiddenOnChannel(CEntityInstance* entity, CPlayerSlot viewer, int channel)
    {
        EntityHook* hook = Find(entity);
        if (!hook || !IsValidSlot(viewer.Get()))
            return false;

        if (channel == TRANSMIT_CHANNEL_ALL)
            return hook->hidden[viewer.Get()] != 0;

        return (hook->hidden[viewer.Get()] >> std::clamp(channel, 0, TRANSMIT_MAX_CHANNELS - 1)) & 1;
    }

    bool TransmitManager::SetBlockAll(CEntityInstance* entity, bool block)
    {
        EntityHook* hook = Find(entity);
        if (!hook)
            return false;

        hook->blockAll = block;
        return true;
    }

    bool TransmitManager::GetBlockAll(CEntityInstance* entity)
    {
        EntityHook* hook = Find(entity);
        return hook && hook->blockAll;
    }

    bool TransmitManager::SetOwner(CEntityInstance* entity, CEntityInstance* owner)
    {
        EntityHook* hook = Find(entity);
        if (!hook)
            return false;

        const int ownerIndex = IndexOf(owner);
        hook->ownerEntity = IsValidIndex(ownerIndex) ? ownerIndex : -1;
        return true;
    }

    CEntityInstance* TransmitManager::GetOwner(CEntityInstance* entity)
    {
        EntityHook* hook = Find(entity);
        if (!hook || hook->ownerEntity < 0 || !shared::g_pEntitySystem)
            return nullptr;

        return shared::g_pEntitySystem->GetEntityInstance(CEntityIndex(hook->ownerEntity));
    }

    void TransmitManager::ResetViewer(CPlayerSlot viewer)
    {
        if (!IsValidSlot(viewer.Get()))
            return;

        for (auto& hook : m_hooks)
        {
            if (hook)
                hook->Reset(viewer.Get());
        }
    }

    /* =========================
    Hooks
    ========================= */

    Source2ModHookId TransmitManager::HookCheckTransmit(PluginId owner, CheckTransmitHook handler)
    {
        return m_checkHooks.Add(owner, std::move(handler));
    }

    bool TransmitManager::UnhookCheckTransmit(PluginId owner, const CheckTransmitHook& handler)
    {
        return m_checkHooks.RemoveHandler(owner, handler);
    }

    bool TransmitManager::UnhookCheckTransmitId(Source2ModHookId id)
    {
        return m_checkHooks.RemoveId(id);
    }

    /* =========================
    CheckTransmit
    ========================= */

    void TransmitManager::OnCheckTransmit(CCheckTransmitInfo** ppInfoList, int infoCount, const uint16_t* pEntityIndices, int nEntities)
    {
        if (infoCount <= 0)
            return;

        const int maxClients = MaxClients();
        int viewCount = 0;

        for (int i = 0; i < infoCount && viewCount < ABSOLUTE_PLAYER_LIMIT; i++)
        {
            auto* info = reinterpret_cast<CCheckTransmitInfo*>(ppInfoList[i]);
            if (!info || !info->m_pTransmitEntity || !info->m_pNonTransmitEntity)
                continue;

            m_views[viewCount].m_pInfo = info;
            m_viewPtrs[viewCount] = &m_views[viewCount];
            viewCount++;

            const int viewerSlot = info->m_nPlayerSlot.Get();
            if (!IsValidSlot(viewerSlot))
                continue;

            const int viewerController = viewerSlot + 1;

            // Players. A controller's hook is applied to the pawns it drives;
            // the controller itself keeps going so the scoreboard stays whole.
            // Left alone on a full update: the client has to have seen a
            // player at least once to keep a record of them afterwards.
            if (!info->m_bFullUpdate)
            {
                for (int c = 1; c <= maxClients; c++)
                {
                    if (c == viewerController)
                        continue;

                    EntityHook* hook = FindByIndex(c);
                    if (!hook || hook->IsVisible(viewerSlot))
                        continue;

                    auto* controller = static_cast<CCSPlayerController*>(shared::g_pEntitySystem->GetEntityInstance(CEntityIndex(c)));
                    if (!controller)
                        continue;

                    if (CCSPlayerPawn* pawn = controller->GetPawn())
                        Block(info, pawn->GetEntityIndex().Get());

                    if (CCSObserverPawn* observer = controller->GetObserverPawn())
                        Block(info, observer->GetEntityIndex().Get());

                    if (CCSPlayerPawn* playerPawn = controller->GetPlayerPawn())
                        Block(info, playerPawn->GetEntityIndex().Get());
                }
            }

            // Everything else, only what the engine put in this tick.
            for (int e = 0; e < nEntities; e++)
            {
                const int index = pEntityIndices[e];
                if (index <= maxClients || !info->m_pTransmitEntity->IsBitSet(index))
                    continue;

                EntityHook* hook = FindByIndex(index);
                if (!hook)
                    continue;

                if (!hook->IsVisible(viewerSlot))
                {
                    Block(info, index);
                    continue;
                }

                if (hook->ownerEntity >= 0)
                {
                    EntityHook* ownerHook = FindByIndex(hook->ownerEntity);
                    if (ownerHook && !ownerHook->IsVisible(viewerSlot))
                        Block(info, index);
                }
            }
        }

        if (viewCount == 0 || m_checkHooks.empty())
            return;

        // A handler may unhook itself; iterate a copy of the list.
        for (const CheckTransmitHook& handler : m_checkHooks.Snapshot())
            handler(m_viewPtrs.data(), viewCount, pEntityIndices, nEntities);
    }

    /* =========================
    Lifetime
    ========================= */

    void TransmitManager::OnClientDisconnect(CPlayerSlot slot)
    {
        ResetViewer(slot);
    }

    void TransmitManager::OnEntityDeleted(CEntityInstance* entity)
    {
        const int index = IndexOf(entity);
        if (!IsValidIndex(index) || !m_hooks[index])
            return;

        if (m_hooks[index]->handle == entity->GetRefEHandle())
            m_hooks[index].reset();
    }

    void TransmitManager::RemoveAllForPlugin(PluginId id)
    {
        for (auto& hook : m_hooks)
        {
            if (hook && hook->owner == id)
                hook.reset();
        }

        m_checkHooks.RemoveOwner(id);
    }

    void TransmitManager::Clear()
    {
        for (auto& hook : m_hooks)
            hook.reset();
    }
}
