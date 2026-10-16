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

/*
 * The ids Register* / Hook* hand out (Source2ModHookId): one counter for every
 * interface, so an id is never shared by two registrations anywhere -- and
 * HookList, the handler list most managers keep.
 */
#pragma once

#include <source2mod/ISource2ModTypes.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace hookid
{
    /// Whose code the address is (PluginManager::OwnerOf); 0 for the core.
    PluginId OwnerOf(const void* address);

    /// The owner of a handler: the plugin its code lies in.
    template <typename HANDLER>
    PluginId OwnerOfHandler(const HANDLER& handler)
    {
        return OwnerOf(handler.Origin());
    }

    inline Source2ModHookId Next()
    {
        static Source2ModHookId s_last = 0;
        return ++s_last;
    }

    /// The handlers of one hook point: who registered each, under which id.
    /// Several per plugin; removed by id, by handler (a function or an object
    /// and a method) or all of a plugin's at once.
    template <typename HANDLER>
    class HookList
    {
    public:
        struct Entry
        {
            PluginId owner;
            Source2ModHookId id;
            HANDLER handler;
        };

        Source2ModHookId Add(PluginId owner, HANDLER handler)
        {
            if (!handler)
                return 0;

            const Source2ModHookId id = Next();
            m_entries.push_back({ owner, id, std::move(handler) });
            return id;
        }

        bool RemoveId(Source2ModHookId id)
        {
            return std::erase_if(m_entries, [id](const Entry& e) { return e.id == id; }) > 0;
        }

        bool RemoveHandler(PluginId owner, const HANDLER& handler)
        {
            if (!handler.HasIdentity())
                return false;

            return std::erase_if(m_entries, [owner, &handler](const Entry& e)
            {
                return e.owner == owner && handler.SameAs(e.handler);
            }) > 0;
        }

        void RemoveOwner(PluginId owner)
        {
            std::erase_if(m_entries, [owner](const Entry& e) { return e.owner == owner; });
        }

        void Clear() { m_entries.clear(); }

        bool empty() const { return m_entries.empty(); }
        size_t size() const { return m_entries.size(); }

        /// Copies of the handlers, for a dispatch: a handler is free to hook
        /// or unhook while it runs.
        std::vector<HANDLER> Snapshot() const
        {
            std::vector<HANDLER> out;
            out.reserve(m_entries.size());
            for (const Entry& e : m_entries)
                out.push_back(e.handler);
            return out;
        }

    private:
        std::vector<Entry> m_entries;
    };
}
