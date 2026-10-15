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
// First: on Windows it brings WinSock2.h, which has to come before windows.h.
#include <maxminddb.h>

#include "core/geoip.h"

#include "core/coreconfig.h"
#include "core/shared.h"
#include "utils/log.h"
#include "utils/paths.h"

#include <source2mod/schema/entity/classes/CCSPlayerController.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace geoip
{
    GeoIPManager geoIpManager;

    namespace
    {
        /// "1.2.3.4:27005" -> "1.2.3.4", "[::1]:27005" -> "::1".
        std::string StripPort(const char* address)
        {
            std::string ip = address ? address : "";
            if (!ip.empty() && ip[0] == '[')
            {
                const size_t close = ip.find(']');
                return close == std::string::npos ? ip.substr(1) : ip.substr(1, close - 1);
            }
            // One colon is IPv4 with a port; more is a bare IPv6 address.
            const size_t colon = ip.find(':');
            if (colon != std::string::npos && ip.find(':', colon + 1) == std::string::npos)
                ip.resize(colon);
            return ip;
        }

        bool Copy(const std::string& value, char* out, size_t maxlen)
        {
            if (out && maxlen)
                snprintf(out, maxlen, "%s", value.c_str());
            return !value.empty();
        }

        void Clear(char* out, size_t maxlen)
        {
            if (out && maxlen)
                out[0] = '\0';
        }

        /// The value at `path` as a string: strings as they are, numbers and bools printed.
        bool ValueAt(MMDB_entry_s& entry, const std::vector<std::string>& path, std::string& out)
        {
            std::vector<const char*> keys;
            for (const std::string& key : path)
                keys.push_back(key.c_str());
            keys.push_back(nullptr);

            MMDB_entry_data_s data{};
            if (MMDB_aget_value(&entry, &data, keys.data()) != MMDB_SUCCESS || !data.has_data)
                return false;

            char buf[64];
            switch (data.type)
            {
            case MMDB_DATA_TYPE_UTF8_STRING: out.assign(data.utf8_string, data.data_size); return true;
            case MMDB_DATA_TYPE_DOUBLE:      snprintf(buf, sizeof(buf), "%.6f", data.double_value); break;
            case MMDB_DATA_TYPE_FLOAT:       snprintf(buf, sizeof(buf), "%.6f", data.float_value); break;
            case MMDB_DATA_TYPE_UINT16:      snprintf(buf, sizeof(buf), "%u", data.uint16); break;
            case MMDB_DATA_TYPE_UINT32:      snprintf(buf, sizeof(buf), "%u", data.uint32); break;
            case MMDB_DATA_TYPE_INT32:       snprintf(buf, sizeof(buf), "%d", data.int32); break;
            case MMDB_DATA_TYPE_UINT64:      snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(data.uint64)); break;
            case MMDB_DATA_TYPE_BOOLEAN:     snprintf(buf, sizeof(buf), "%s", data.boolean ? "true" : "false"); break;
            default:                         return false;
            }
            out = buf;
            return true;
        }

        bool Find(MMDB_s* db, const char* address, MMDB_entry_s& entry)
        {
            if (!db)
                return false;
            const std::string ip = StripPort(address);
            if (ip.empty())
                return false;

            int gaiError = 0, mmdbError = MMDB_SUCCESS;
            MMDB_lookup_result_s result = MMDB_lookup_string(db, ip.c_str(), &gaiError, &mmdbError);
            if (gaiError != 0 || mmdbError != MMDB_SUCCESS || !result.found_entry)
                return false;
            entry = result.entry;
            return true;
        }

        std::vector<std::string> Split(const char* path)
        {
            std::vector<std::string> keys;
            std::string key;
            for (const char* c = path; c && *c; c++)
            {
                if (*c == '/')
                {
                    if (!key.empty())
                        keys.push_back(std::move(key));
                    key.clear();
                }
                else
                    key += *c;
            }
            if (!key.empty())
                keys.push_back(std::move(key));
            return keys;
        }
    }

    void GeoIPManager::Init()
    {
        std::string configured = shared::g_pCoreConfig ? shared::g_pCoreConfig->GeoIPDatabase : std::string();
        if (configured.empty())
            configured = "geoip/GeoLite2-City.mmdb";

        std::filesystem::path path(configured);
        if (!path.is_absolute())
            path = std::filesystem::path(paths::GetRootDirectory()) / path;

        // The City database is the default; the Country one next to it does too.
        std::vector<std::filesystem::path> tries{ path };
        if (path.filename() == "GeoLite2-City.mmdb")
            tries.push_back(path.parent_path() / "GeoLite2-Country.mmdb");

        for (const auto& candidate : tries)
        {
            std::error_code ec;
            if (!std::filesystem::exists(candidate, ec))
                continue;

            auto* db = new MMDB_s{};
            const int status = MMDB_open(candidate.string().c_str(), MMDB_MODE_MMAP, db);
            if (status != MMDB_SUCCESS)
            {
                FP_ERROR("GeoIP: could not open '{}': {}", candidate.string(), MMDB_strerror(status));
                delete db;
                continue;
            }
            m_db = db;
            m_path = candidate.string();
            FP_INFO("GeoIP: {} loaded from '{}'", GetDatabaseType(), m_path);
            return;
        }

        FP_WARN("GeoIP: no database at '{}' -- GeoIP lookups are off. Download GeoLite2-City.mmdb (or GeoLite2-Country.mmdb) "
                "from MaxMind and put it there, or point core.json's \"GeoIPDatabase\" at it.", path.string());
    }

    void GeoIPManager::Shutdown()
    {
        if (m_db)
        {
            MMDB_close(m_db);
            delete m_db;
            m_db = nullptr;
        }
    }

    const char* GeoIPManager::GetDatabaseType()
    {
        return m_db && m_db->metadata.database_type ? m_db->metadata.database_type : "";
    }

    bool GeoIPManager::Lookup(const char* ip, Source2ModGeoIPField field, char* out, size_t maxlen, const char* language)
    {
        Clear(out, maxlen);
        MMDB_entry_s entry{};
        if (!Find(m_db, ip, entry))
            return false;

        const std::string lang = language && *language ? language : "en";
        std::string value;

        // A name in the language asked for, else in English.
        auto name = [&](std::vector<std::string> path)
        {
            path.push_back("names");
            path.push_back(lang);
            if (ValueAt(entry, path, value))
                return true;
            path.back() = "en";
            return lang != "en" && ValueAt(entry, path, value);
        };

        bool ok = false;
        switch (field)
        {
        case SOURCE2MOD_GEOIP_COUNTRY_CODE:   ok = ValueAt(entry, { "country", "iso_code" }, value); break;
        case SOURCE2MOD_GEOIP_COUNTRY:        ok = name({ "country" }); break;
        case SOURCE2MOD_GEOIP_CONTINENT_CODE: ok = ValueAt(entry, { "continent", "code" }, value); break;
        case SOURCE2MOD_GEOIP_CONTINENT:      ok = name({ "continent" }); break;
        case SOURCE2MOD_GEOIP_REGION_CODE:    ok = ValueAt(entry, { "subdivisions", "0", "iso_code" }, value); break;
        case SOURCE2MOD_GEOIP_REGION:         ok = name({ "subdivisions", "0" }); break;
        case SOURCE2MOD_GEOIP_CITY:           ok = name({ "city" }); break;
        case SOURCE2MOD_GEOIP_TIMEZONE:       ok = ValueAt(entry, { "location", "time_zone" }, value); break;
        case SOURCE2MOD_GEOIP_POSTAL_CODE:    ok = ValueAt(entry, { "postal", "code" }, value); break;
        }
        return ok && Copy(value, out, maxlen);
    }

    bool GeoIPManager::LookupPlayer(CPlayerSlot slot, Source2ModGeoIPField field, char* out, size_t maxlen, const char* language)
    {
        char ip[64];
        if (!GetPlayerIP(slot, ip, sizeof(ip)))
        {
            Clear(out, maxlen);
            return false;
        }
        return Lookup(ip, field, out, maxlen, language);
    }

    bool GeoIPManager::GetLocation(const char* ip, double& latitude, double& longitude)
    {
        MMDB_entry_s entry{};
        if (!Find(m_db, ip, entry))
            return false;

        MMDB_entry_data_s lat{}, lon{};
        if (MMDB_get_value(&entry, &lat, "location", "latitude", nullptr) != MMDB_SUCCESS || !lat.has_data || lat.type != MMDB_DATA_TYPE_DOUBLE)
            return false;
        if (MMDB_get_value(&entry, &lon, "location", "longitude", nullptr) != MMDB_SUCCESS || !lon.has_data || lon.type != MMDB_DATA_TYPE_DOUBLE)
            return false;
        latitude = lat.double_value;
        longitude = lon.double_value;
        return true;
    }

    bool GeoIPManager::LookupPath(const char* ip, const char* path, char* out, size_t maxlen)
    {
        Clear(out, maxlen);
        MMDB_entry_s entry{};
        const std::vector<std::string> keys = Split(path);
        std::string value;
        if (keys.empty() || !Find(m_db, ip, entry) || !ValueAt(entry, keys, value))
            return false;
        return Copy(value, out, maxlen);
    }

    bool GeoIPManager::GetPlayerIP(CPlayerSlot slot, char* out, size_t maxlen)
    {
        Clear(out, maxlen);
        CCSPlayerController* controller = CCSPlayerController::FromSlot(slot);
        if (!controller)
            return false;
        const std::string ip = StripPort(controller->GetIpAddress().Get());
        // Bots have no address (or "loopback").
        if (ip.empty() || ip == "loopback")
            return false;
        return Copy(ip, out, maxlen);
    }
}
