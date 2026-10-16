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
 * Authors:
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */
#include "core/cookies.h"

#include "core/coreconfig.h"
#include "core/hookid.h"
#include "core/permissions.h"
#include "core/scheduler.h"
#include "core/shared.h"
#include "sourcepawn/scriptplugin.h"
#include "utils/log.h"
#include "utils/paths.h"

#include <sqlite3.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>

namespace cookies
{
    CookiesManager cookiesManager;

    namespace
    {
        constexpr int kMaxSlots = 64;

        /// A prepared statement that finalizes itself.
        struct Statement
        {
            Statement(sqlite3* db, const char* sql)
            {
                if (db && sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
                {
                    FP_ERROR("ClientPrefs: '{}': {}", sql, sqlite3_errmsg(db));
                    stmt = nullptr;
                }
            }
            ~Statement() { sqlite3_finalize(stmt); }

            Statement& Text(int index, const std::string& value)
            {
                if (stmt)
                    sqlite3_bind_text(stmt, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
                return *this;
            }

            Statement& Int(int index, int64_t value)
            {
                if (stmt)
                    sqlite3_bind_int64(stmt, index, value);
                return *this;
            }

            std::string Column(int index) const
            {
                const unsigned char* text = sqlite3_column_text(stmt, index);
                return text ? reinterpret_cast<const char*>(text) : "";
            }

            sqlite3_stmt* stmt = nullptr;
        };

        bool ValidSlot(CPlayerSlot slot)
        {
            return slot.Get() >= 0 && slot.Get() < kMaxSlots;
        }
    }

    std::string CookiesManager::AuthId(uint64 steamId)
    {
        const uint32_t account = static_cast<uint32_t>(steamId & 0xFFFFFFFFull);
        char buf[48];
        snprintf(buf, sizeof(buf), "STEAM_1:%u:%u", account & 1u, account >> 1);
        return buf;
    }

    uint64 CookiesManager::ParseAuthId(const char* authId)
    {
        if (!authId || !*authId)
            return 0;

        unsigned int universe = 0, y = 0, z = 0;
        if (sscanf(authId, "STEAM_%u:%u:%u", &universe, &y, &z) == 3)
            return 76561197960265728ull + (static_cast<uint64>(z) << 1) + y;
        if (sscanf(authId, "[U:1:%u]", &z) == 1)
            return 76561197960265728ull + z;

        char* end = nullptr;
        const unsigned long long id = strtoull(authId, &end, 10);
        return end && *end == '\0' && id > 76561197960265728ull ? static_cast<uint64>(id) : 0;
    }

    // ---- database ---------------------------------------------------------------

    void CookiesManager::Init()
    {
        std::string configured = shared::g_pCoreConfig ? shared::g_pCoreConfig->ClientPrefsDatabase : std::string();
        if (configured.empty())
            configured = "data/clientprefs.sq3";

        std::filesystem::path path(configured);
        if (!path.is_absolute())
            path = std::filesystem::path(paths::GetRootDirectory()) / path;

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);

        if (sqlite3_open_v2(path.string().c_str(), &m_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK)
        {
            FP_ERROR("ClientPrefs: could not open '{}': {} -- cookies are kept for this session only", path.string(), m_db ? sqlite3_errmsg(m_db) : "out of memory");
            sqlite3_close(m_db);
            m_db = nullptr;
            return;
        }

        // A write-ahead log and no fsync per commit: a cookie write costs a
        // page write, not a disk flush, on the main thread.
        Exec("PRAGMA journal_mode=WAL");
        Exec("PRAGMA synchronous=NORMAL");
        sqlite3_busy_timeout(m_db, 1000);

        // SourceMod's tables, so a SourceMod clientprefs database carries over.
        if (!Exec("CREATE TABLE IF NOT EXISTS sm_cookies (id INTEGER PRIMARY KEY AUTOINCREMENT, name varchar(30) NOT NULL UNIQUE, "
                  "description varchar(255), access INTEGER)")
            || !Exec("CREATE TABLE IF NOT EXISTS sm_cookie_cache (player varchar(65) NOT NULL, cookie_id int(10) NOT NULL, "
                     "value varchar(100), timestamp int, PRIMARY KEY (player, cookie_id))"))
        {
            sqlite3_close(m_db);
            m_db = nullptr;
            return;
        }

        FP_INFO("ClientPrefs: database '{}'", path.string());

        // Loaded mid-map: the players already authorized get theirs now.
        for (int slot = 0; slot < kMaxSlots; slot++)
        {
            if (permissions::permissionsManager.IsPlayerAuthorized(CPlayerSlot(slot)))
                if (const uint64 steamId = permissions::permissionsManager.GetPlayerSteamID(CPlayerSlot(slot)))
                    OnClientAuthorized(CPlayerSlot(slot), steamId);
        }
    }

    void CookiesManager::Shutdown()
    {
        m_listeners.clear();
        if (m_db)
        {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
    }

    bool CookiesManager::Exec(const char* sql)
    {
        char* error = nullptr;
        if (sqlite3_exec(m_db, sql, nullptr, nullptr, &error) != SQLITE_OK)
        {
            FP_ERROR("ClientPrefs: '{}': {}", sql, error ? error : "?");
            sqlite3_free(error);
            return false;
        }
        return true;
    }

    int64_t CookiesManager::EnsureDbId(Cookie& cookie)
    {
        if (cookie.dbId || !m_db)
            return cookie.dbId;

        Statement insert(m_db, "INSERT OR IGNORE INTO sm_cookies (name, description, access) VALUES (?, ?, ?)");
        insert.Text(1, cookie.name).Text(2, cookie.description).Int(3, cookie.access);
        if (insert.stmt)
            sqlite3_step(insert.stmt);

        Statement select(m_db, "SELECT id FROM sm_cookies WHERE name = ?");
        select.Text(1, cookie.name);
        if (select.stmt && sqlite3_step(select.stmt) == SQLITE_ROW)
            cookie.dbId = sqlite3_column_int64(select.stmt, 0);
        return cookie.dbId;
    }

    void CookiesManager::Save(uint64 steamId, Cookie& cookie, const Value& value)
    {
        if (!m_db || !EnsureDbId(cookie))
            return;

        Statement save(m_db, "INSERT OR REPLACE INTO sm_cookie_cache (player, cookie_id, value, timestamp) VALUES (?, ?, ?, ?)");
        save.Text(1, AuthId(steamId)).Int(2, cookie.dbId).Text(3, value.value).Int(4, value.time);
        if (save.stmt && sqlite3_step(save.stmt) != SQLITE_DONE)
            FP_ERROR("ClientPrefs: saving '{}' for {}: {}", cookie.name, AuthId(steamId), sqlite3_errmsg(m_db));
    }

    void CookiesManager::Load(int slot)
    {
        Player& player = m_players[slot];
        if (m_db)
        {
            Statement load(m_db, "SELECT sm_cookies.name, sm_cookie_cache.value, sm_cookie_cache.timestamp FROM sm_cookie_cache "
                                 "JOIN sm_cookies ON sm_cookies.id = sm_cookie_cache.cookie_id WHERE sm_cookie_cache.player = ?");
            load.Text(1, AuthId(player.steamId));
            while (load.stmt && sqlite3_step(load.stmt) == SQLITE_ROW)
            {
                Value& value = player.values[load.Column(0)];
                if (value.early)
                    continue;
                value.value = load.Column(1);
                value.time = sqlite3_column_int64(load.stmt, 2);
            }
        }

        // What was set before the load goes to the database now.
        for (auto& [name, value] : player.values)
        {
            if (!value.early)
                continue;
            value.early = false;
            if (const Source2ModCookieId id = FindCookie(name.c_str()))
                Save(player.steamId, m_cookies[static_cast<size_t>(id - 1)], value);
        }
        player.cached = true;

        // Next frame: every plugin has had its OnClientAuthorized by then.
        const uint64 steamId = player.steamId;
        scheduler::schedulerManager.NextFrame(0, std::function<void()>([this, slot, steamId]() { Cached(slot, steamId); }));
    }

    void CookiesManager::Cached(int slot, uint64 steamId)
    {
        // Gone, or somebody else in the slot, since.
        if (m_players[slot].steamId != steamId || !m_players[slot].cached)
            return;

        // Copied: a listener may add or remove listeners.
        const std::vector<Listener> listeners = m_listeners;
        for (const Listener& l : listeners)
            l.callback(CPlayerSlot(slot));

        for (sourcepawn::ScriptPlugin* script : sourcepawn::ScriptPlugin::Running())
            script->CallForwardClient("Source2Mod_OnClientCookiesCached", slot);
    }

    // ---- players -----------------------------------------------------------------

    void CookiesManager::OnClientAuthorized(CPlayerSlot slot, uint64 steamId)
    {
        if (!ValidSlot(slot) || !steamId)
            return;
        Player& player = m_players[slot.Get()];
        if (player.steamId == steamId && player.cached)
            return;
        player.steamId = steamId;
        player.cached = false;
        Load(slot.Get());
    }

    void CookiesManager::OnClientDisconnect(CPlayerSlot slot)
    {
        if (ValidSlot(slot))
            m_players[slot.Get()] = Player{};
    }

    CookiesManager::Player* CookiesManager::PlayerAt(CPlayerSlot slot)
    {
        return ValidSlot(slot) ? &m_players[slot.Get()] : nullptr;
    }

    bool CookiesManager::AreCookiesCached(CPlayerSlot slot)
    {
        Player* player = PlayerAt(slot);
        return player && player->cached;
    }

    // ---- cookies -----------------------------------------------------------------

    const CookiesManager::Cookie* CookiesManager::Get(Source2ModCookieId cookie) const
    {
        return cookie >= 1 && static_cast<size_t>(cookie) <= m_cookies.size() ? &m_cookies[static_cast<size_t>(cookie - 1)] : nullptr;
    }

    Source2ModCookieId CookiesManager::RegisterCookie(const char* name, const char* description, Source2ModCookieAccess access)
    {
        if (!name || !*name || strlen(name) > SOURCE2MOD_COOKIE_MAX_NAME_LENGTH)
            return 0;
        if (const Source2ModCookieId existing = FindCookie(name))
            return existing;

        Cookie cookie;
        cookie.name = name;
        cookie.description = description ? description : "";
        cookie.access = access;
        EnsureDbId(cookie);
        m_cookies.push_back(std::move(cookie));
        return static_cast<Source2ModCookieId>(m_cookies.size());
    }

    Source2ModCookieId CookiesManager::FindCookie(const char* name)
    {
        if (!name)
            return 0;
        for (size_t i = 0; i < m_cookies.size(); i++)
            if (m_cookies[i].name == name)
                return static_cast<Source2ModCookieId>(i + 1);
        return 0;
    }

    const char* CookiesManager::GetCookieName(Source2ModCookieId cookie)
    {
        const Cookie* c = Get(cookie);
        return c ? c->name.c_str() : nullptr;
    }

    const char* CookiesManager::GetCookieDescription(Source2ModCookieId cookie)
    {
        const Cookie* c = Get(cookie);
        return c ? c->description.c_str() : nullptr;
    }

    Source2ModCookieAccess CookiesManager::GetCookieAccess(Source2ModCookieId cookie)
    {
        const Cookie* c = Get(cookie);
        return c ? c->access : SOURCE2MOD_COOKIE_PUBLIC;
    }

    // ---- values ------------------------------------------------------------------

    bool CookiesManager::GetCookie(CPlayerSlot slot, Source2ModCookieId cookie, char* out, size_t maxlen)
    {
        if (out && maxlen)
            out[0] = '\0';
        Player* player = PlayerAt(slot);
        const Cookie* c = Get(cookie);
        if (!player || !c)
            return false;
        auto it = player->values.find(c->name);
        if (it == player->values.end() || (!player->cached && !it->second.early))
            return false;
        if (out && maxlen)
            snprintf(out, maxlen, "%s", it->second.value.c_str());
        return true;
    }

    bool CookiesManager::SetCookie(CPlayerSlot slot, Source2ModCookieId cookie, const char* value)
    {
        Player* player = PlayerAt(slot);
        if (!player || !Get(cookie) || !value || strlen(value) > SOURCE2MOD_COOKIE_MAX_VALUE_LENGTH)
            return false;
        Cookie& c = m_cookies[static_cast<size_t>(cookie - 1)];

        // A bot or a player not authorized yet: nothing to key the value by
        // until OnClientAuthorized, which keeps an early value.
        Value& v = player->values[c.name];
        v.value = value;
        v.time = static_cast<int64_t>(std::time(nullptr));
        if (!player->cached)
        {
            v.early = true;
            return true;
        }
        Save(player->steamId, c, v);
        return true;
    }

    int64_t CookiesManager::GetCookieTime(CPlayerSlot slot, Source2ModCookieId cookie)
    {
        Player* player = PlayerAt(slot);
        const Cookie* c = Get(cookie);
        if (!player || !c)
            return 0;
        auto it = player->values.find(c->name);
        return it == player->values.end() ? 0 : it->second.time;
    }

    bool CookiesManager::SetCookieBySteamID(uint64 steamId, Source2ModCookieId cookie, const char* value)
    {
        if (!steamId || !Get(cookie) || !value || strlen(value) > SOURCE2MOD_COOKIE_MAX_VALUE_LENGTH)
            return false;

        // Connected: through the slot, so the cache follows.
        for (int slot = 0; slot < kMaxSlots; slot++)
        {
            if (m_players[slot].steamId == steamId && m_players[slot].cached)
                return SetCookie(CPlayerSlot(slot), cookie, value);
        }

        Value v{ value, static_cast<int64_t>(std::time(nullptr)), false };
        Save(steamId, m_cookies[static_cast<size_t>(cookie - 1)], v);
        return true;
    }

    // ---- listeners ---------------------------------------------------------------

    Source2ModCookiesListenerId CookiesManager::AddCookiesCachedListener(CookiesCachedCallback callback)
    {
        const PluginId owner = hookid::OwnerOfHandler(callback);
        const Source2ModHookId id = hookid::Next();
        m_listeners.push_back({ id, owner, std::move(callback) });
        return id;
    }

    bool CookiesManager::RemoveCookiesCachedListener(Source2ModCookiesListenerId id)
    {
        const size_t before = m_listeners.size();
        std::erase_if(m_listeners, [id](const Listener& l) { return l.id == id; });
        return m_listeners.size() != before;
    }

    void CookiesManager::RemoveAllForPlugin(PluginId id)
    {
        std::erase_if(m_listeners, [id](const Listener& l) { return l.owner == id; });
    }
}
