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

#include <source2mod/ISource2ModGeoIP.h>

#include <string>

struct MMDB_s;

namespace geoip
{
    /// ISource2ModGeoIP on libmaxminddb and a GeoLite2 database.
    class GeoIPManager : public ISource2ModGeoIP
    {
    public:
        bool IsLoaded() override { return m_db != nullptr; }
        const char* GetDatabaseType() override;
        bool Lookup(const char* ip, Source2ModGeoIPField field, char* out, size_t maxlen, const char* language = nullptr) override;
        bool LookupPlayer(CPlayerSlot slot, Source2ModGeoIPField field, char* out, size_t maxlen, const char* language = nullptr) override;
        bool GetLocation(const char* ip, double& latitude, double& longitude) override;
        bool LookupPath(const char* ip, const char* path, char* out, size_t maxlen) override;
        bool GetPlayerIP(CPlayerSlot slot, char* out, size_t maxlen) override;

        /// Opens core.json's "GeoIPDatabase"; logs where it looked when it is not there.
        void Init();
        void Shutdown();

    private:
        MMDB_s* m_db = nullptr;
        std::string m_path;
    };

    extern GeoIPManager geoIpManager;
}
