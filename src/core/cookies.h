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
#pragma once

#include <source2mod/ISource2ModCookies.h>

#include <string>
#include <unordered_map>
#include <vector>

struct sqlite3;

namespace cookies
{
    /// ISource2ModCookies: SourceMod's clientprefs on a database of SourceMod's layout.
    class CookiesManager : public ISource2ModCookies
    {
    public:
        bool IsConnected() override { return m_db != nullptr; }

        Source2ModCookieId RegisterCookie(const char* name, const char* description, Source2ModCookieAccess access) override;
        Source2ModCookieId FindCookie(const char* name) override;
        const char* GetCookieName(Source2ModCookieId cookie) override;
        const char* GetCookieDescription(Source2ModCookieId cookie) override;
        Source2ModCookieAccess GetCookieAccess(Source2ModCookieId cookie) override;
        int GetCookieCount() override { return static_cast<int>(m_cookies.size()); }

        bool AreCookiesCached(CPlayerSlot slot) override;
        bool GetCookie(CPlayerSlot slot, Source2ModCookieId cookie, char* out, size_t maxlen) override;
        bool SetCookie(CPlayerSlot slot, Source2ModCookieId cookie, const char* value) override;
        int64_t GetCookieTime(CPlayerSlot slot, Source2ModCookieId cookie) override;
        bool SetCookieBySteamID(uint64 steamId, Source2ModCookieId cookie, const char* value) override;

        Source2ModHookId AddCookiesCachedListener(CookiesCachedCallback callback) override;
        bool RemoveCookiesCachedListener(Source2ModHookId id) override;

        /// Opens the database (and loads the players already authorized, for a late load).
        void Init();
        void Shutdown();

        void OnClientAuthorized(CPlayerSlot slot, uint64 steamId);
        void OnClientDisconnect(CPlayerSlot slot);
        void RemoveAllForPlugin(PluginId id);

        /// SteamID64 -> "STEAM_1:Y:Z", how SourceMod's tables key players.
        static std::string AuthId(uint64 steamId);
        /// "STEAM_1:Y:Z", "STEAM_0:Y:Z", "[U:1:N]" or a SteamID64 -> SteamID64; 0 when it is none.
        static uint64 ParseAuthId(const char* authId);

    private:
        struct Cookie
        {
            std::string name;
            std::string description;
            Source2ModCookieAccess access = SOURCE2MOD_COOKIE_PUBLIC;
            int64_t dbId = 0;
        };

        struct Value
        {
            std::string value;
            int64_t time = 0;
            /// Set before the player's values arrived: wins over the loaded one.
            bool early = false;
        };

        struct Player
        {
            uint64 steamId = 0;
            bool cached = false;
            /// By cookie name: a value loaded for a cookie no plugin registered
            /// yet is there once one does.
            std::unordered_map<std::string, Value> values;
        };

        struct Listener
        {
            Source2ModHookId id;
            PluginId owner;
            CookiesCachedCallback callback;
        };

        const Cookie* Get(Source2ModCookieId cookie) const;
        Player* PlayerAt(CPlayerSlot slot);
        bool Exec(const char* sql);
        int64_t EnsureDbId(Cookie& cookie);
        void Save(uint64 steamId, Cookie& cookie, const Value& value);
        void Load(int slot);
        void Cached(int slot, uint64 steamId);

        sqlite3* m_db = nullptr;
        std::vector<Cookie> m_cookies;   // id = index + 1
        Player m_players[64];
        std::vector<Listener> m_listeners;
    };

    extern CookiesManager cookiesManager;
}
