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
#include "customhud.h"

#include "hookid.h"
#include "utils/log.h"

#include <source2mod/schema/entity/classes/CCSCustomHudLayout.h>
#include <source2mod/schema/entity/classes/CCSPlayerController.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace customhud
{
    CustomHudManager customHudManager;

    Source2ModCustomHudHookId CustomHudManager::HookCustomHudClick(PluginId owner, CCSCustomHudLayout* pLayout, CustomHudClickHandler handler)
    {
        if (!pLayout || !handler)
            return 0;

        const Source2ModHookId id = hookid::Next();
        m_callbacks.push_back({ owner, CHandle<CCSCustomHudLayout>(pLayout), std::move(handler), id });
        return id;
    }

    bool CustomHudManager::UnhookCustomHudClick(PluginId owner, CCSCustomHudLayout* pLayout, const CustomHudClickHandler& handler)
    {
        if (!pLayout || !handler.HasIdentity())
            return false;

        const CHandle<CCSCustomHudLayout> handle(pLayout);
        return std::erase_if(m_callbacks, [owner, &handle, &handler](const ClickCallbackEntry& e)
        {
            return e.owner == owner && e.layout == handle && handler.SameAs(e.handler);
        }) > 0;
    }

    bool CustomHudManager::UnhookCustomHudClickId(Source2ModHookId id)
    {
        return std::erase_if(m_callbacks, [id](const ClickCallbackEntry& e) { return e.id == id; }) > 0;
    }

    void CustomHudManager::UnhookCustomHudClick(CCSCustomHudLayout* pLayout)
    {
        if (!pLayout)
            return;

        const CHandle<CCSCustomHudLayout> handle(pLayout);

        std::erase_if(m_callbacks, [&handle](const ClickCallbackEntry& e)
        {
            return e.layout == handle;
        });
    }

    namespace
    {
        bool ReadVarint(const uint8_t*& p, const uint8_t* end, uint64_t& out)
        {
            out = 0;
            for (int shift = 0; p < end && shift < 64; shift += 7)
            {
                const uint8_t b = *p++;
                out |= static_cast<uint64_t>(b & 0x7F) << shift;
                if (!(b & 0x80))
                    return true;
            }
            return false;
        }

        /// Wire-format decode of CCSUsrMsg_CustomHudClicked; unknown fields
        /// are skipped so a field Valve adds later does not break it.
        bool DecodeClick(const uint8_t* p, uint32 nSize, uint32& nHandle, std::string& sButtonId)
        {
            const uint8_t* end = p + nSize;
            while (p < end)
            {
                uint64_t key;
                if (!ReadVarint(p, end, key)) return false;
                const uint32 field = static_cast<uint32>(key >> 3);
                const uint32 wire = static_cast<uint32>(key & 7);

                if (wire == 0)
                {
                    uint64_t v;
                    if (!ReadVarint(p, end, v)) return false;
                    if (field == 1) nHandle = static_cast<uint32>(v);
                }
                else if (wire == 2)
                {
                    uint64_t len;
                    if (!ReadVarint(p, end, len) || len > static_cast<uint64_t>(end - p)) return false;
                    if (field == 2) sButtonId.assign(reinterpret_cast<const char*>(p), static_cast<size_t>(len));
                    p += len;
                }
                else if (wire == 5) { if (end - p < 4) return false; p += 4; }
                else if (wire == 1) { if (end - p < 8) return false; p += 8; }
                else return false;
            }
            return true;
        }
    }

    void CustomHudManager::HandleClick(CCSPlayerController* pController, const void* pBuffer, uint32 nSize)
    {
        if (!pController || !pBuffer)
            return;

        if (m_callbacks.empty())
            return;

        // CCSUsrMsg_CustomHudClicked is two fields -- custom_hud_layout
        // (1, uint32) and button_id (2, string) -- decoded here by hand: the
        // message is newer than the protobuf checkout Source2Mod builds
        // against, and asking the engine for a message object to parse into
        // is one more thing that can quietly fail.
        uint32 nPackedHandle = 0xFFFFFF;
        std::string sButtonId;

        if (!DecodeClick(static_cast<const uint8_t*>(pBuffer), nSize, nPackedHandle, sButtonId))
        {
            FP_WARN("custom HUD click from slot {}: {}-byte payload did not decode", pController->GetPlayerSlot().Get(), nSize);
            return;
        }

        // Low 14 bits of a packed handle are the entity index; the stored
        // CHandle carries the serial, so a recycled index cannot match.
        const int nEntryIndex = static_cast<int>(nPackedHandle & 0x3FFF);

        // Collect first, fire second: a handler is free to open another layout
        // or drop its own callback, either of which would move this vector out
        // from under an iterator.
        std::vector<std::pair<CCSCustomHudLayout*, CustomHudClickHandler>> fire;

        for (auto it = m_callbacks.begin(); it != m_callbacks.end();)
        {
            CCSCustomHudLayout* pLayout = it->layout.Get();

            // Prune callbacks whose layout is gone while we are walking anyway.
            if (!pLayout)
            {
                it = m_callbacks.erase(it);
                continue;
            }

            if (it->layout.GetEntryIndex() == nEntryIndex)
                fire.emplace_back(pLayout, it->handler);

            ++it;
        }

        for (auto& [pLayout, handler] : fire)
            handler(pController, pLayout, sButtonId.c_str());
    }

    void CustomHudManager::RemoveAllForPlugin(PluginId id)
    {
        std::erase_if(m_callbacks, [id](const ClickCallbackEntry& e)
        {
            return e.owner == id;
        });
    }

    void CustomHudManager::Clear()
    {
        m_callbacks.clear();
    }
}
