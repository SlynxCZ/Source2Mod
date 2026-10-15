/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * SourcePawn natives for GeoIP: Source2ModGeoIP (source2mod/geoip.inc) and
 * SourceMod's geoip.inc (GeoipCode2, GeoipCountry, GeoipCity, ...), both on
 * ISource2ModGeoIP. GeoipCode3 is not there: GeoLite2 has no three-letter codes.
 *
 * Project: Source2Mod
 */
#include "natives.h"

#include "core/geoip.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        geoip::GeoIPManager& Geo()
        {
            return geoip::geoIpManager;
        }

        bool FieldOf(IPluginContext* ctx, cell_t value, Source2ModGeoIPField& field)
        {
            if (value < SOURCE2MOD_GEOIP_COUNTRY_CODE || value > SOURCE2MOD_GEOIP_POSTAL_CODE)
            {
                ctx->ThrowNativeError("Invalid GeoIP field %d", value);
                return false;
            }
            field = static_cast<Source2ModGeoIPField>(value);
            return true;
        }

        /// A CPlayerSlot argument (source2mod/*.inc pass slots, 0-based).
        bool PlayerSlot(IPluginContext* ctx, cell_t value, CPlayerSlot& slot)
        {
            if (value < 0 || value >= kMaxClients)
            {
                ctx->ThrowNativeError("Player slot %d is invalid", value);
                return false;
            }
            slot = CPlayerSlot(value);
            return true;
        }

        /// A lookup into a script buffer.
        cell_t Field(IPluginContext* ctx, const char* ip, Source2ModGeoIPField field, cell_t addr, cell_t maxlen, const char* language = nullptr)
        {
            char value[256];
            const bool ok = Geo().Lookup(ip, field, value, sizeof(value), language);
            ctx->StringToLocalUTF8(addr, maxlen, ok ? value : "", nullptr);
            return ok;
        }

        // ---- Source2ModGeoIP -----------------------------------------------------

        cell_t N_GeoIsLoaded(IPluginContext*, const cell_t*)
        {
            return Geo().IsLoaded();
        }

        cell_t N_GeoGetDatabaseType(IPluginContext* ctx, const cell_t* params)
        {
            ctx->StringToLocalUTF8(params[1], params[2], Geo().GetDatabaseType(), nullptr);
            return 0;
        }

        // (const char[] ip, GeoIPField field, char[] buffer, int maxlen, const char[] language="en")
        cell_t N_GeoLookup(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModGeoIPField field;
            if (!FieldOf(ctx, params[2], field))
                return 0;
            return Field(ctx, LocalString(ctx, params[1]), field, params[3], params[4], LocalString(ctx, params[5]));
        }

        // (CPlayerSlot slot, GeoIPField field, char[] buffer, int maxlen, const char[] language="en")
        cell_t N_GeoLookupPlayer(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModGeoIPField field;
            CPlayerSlot slot(0);
            if (!FieldOf(ctx, params[2], field) || !PlayerSlot(ctx, params[1], slot))
                return 0;
            char value[256];
            const bool ok = Geo().LookupPlayer(slot, field, value, sizeof(value), LocalString(ctx, params[5]));
            ctx->StringToLocalUTF8(params[3], params[4], ok ? value : "", nullptr);
            return ok;
        }

        // (const char[] ip, float &latitude, float &longitude)
        cell_t N_GeoGetLocation(IPluginContext* ctx, const cell_t* params)
        {
            double lat = 0.0, lon = 0.0;
            if (!Geo().GetLocation(LocalString(ctx, params[1]), lat, lon))
                return 0;
            cell_t* out;
            ctx->LocalToPhysAddr(params[2], &out);
            *out = sp_ftoc(static_cast<float>(lat));
            ctx->LocalToPhysAddr(params[3], &out);
            *out = sp_ftoc(static_cast<float>(lon));
            return 1;
        }

        // (const char[] ip, const char[] path, char[] buffer, int maxlen)
        cell_t N_GeoLookupPath(IPluginContext* ctx, const cell_t* params)
        {
            char value[256];
            const bool ok = Geo().LookupPath(LocalString(ctx, params[1]), LocalString(ctx, params[2]), value, sizeof(value));
            ctx->StringToLocalUTF8(params[3], params[4], ok ? value : "", nullptr);
            return ok;
        }

        // (CPlayerSlot slot, char[] buffer, int maxlen)
        cell_t N_GeoGetPlayerIP(IPluginContext* ctx, const cell_t* params)
        {
            CPlayerSlot slot(0);
            if (!PlayerSlot(ctx, params[1], slot))
                return 0;
            char ip[64];
            const bool ok = Geo().GetPlayerIP(slot, ip, sizeof(ip));
            ctx->StringToLocalUTF8(params[2], params[3], ok ? ip : "", nullptr);
            return ok;
        }

        // ---- SourceMod geoip.inc -------------------------------------------------

        // native bool GeoipCode2(const char[] ip, char ccode[3]);
        cell_t N_GeoipCode2(IPluginContext* ctx, const cell_t* params)
        {
            return Field(ctx, LocalString(ctx, params[1]), SOURCE2MOD_GEOIP_COUNTRY_CODE, params[2], 3);
        }

        // native bool GeoipRegionCode(const char[] ip, char ccode[12]);
        cell_t N_GeoipRegionCode(IPluginContext* ctx, const cell_t* params)
        {
            // SourceMod's is "<country>-<subdivision>", "US-CA".
            const char* ip = LocalString(ctx, params[1]);
            char country[8], region[16], code[32] = "";
            const bool ok = Geo().Lookup(ip, SOURCE2MOD_GEOIP_COUNTRY_CODE, country, sizeof(country))
                         && Geo().Lookup(ip, SOURCE2MOD_GEOIP_REGION_CODE, region, sizeof(region));
            if (ok)
                snprintf(code, sizeof(code), "%s-%s", country, region);
            ctx->StringToLocalUTF8(params[2], 12, code, nullptr);
            return ok;
        }

        // native Continent GeoipContinentCode(const char[] ip, char ccode[3]);
        cell_t N_GeoipContinentCode(IPluginContext* ctx, const cell_t* params)
        {
            char code[8];
            if (!Geo().Lookup(LocalString(ctx, params[1]), SOURCE2MOD_GEOIP_CONTINENT_CODE, code, sizeof(code)))
            {
                ctx->StringToLocalUTF8(params[2], 3, "", nullptr);
                return 0;
            }
            ctx->StringToLocalUTF8(params[2], 3, code, nullptr);
            static const char* const s_codes[] = { "AF", "AN", "AS", "EU", "NA", "OC", "SA" };
            for (cell_t i = 0; i < 7; i++)
                if (!strcmp(code, s_codes[i]))
                    return i + 1;
            return 0;
        }

        // native bool GeoipCountry(const char[] ip, char[] name, int maxlength);
        cell_t N_GeoipCountry(IPluginContext* ctx, const cell_t* params)
        {
            return Field(ctx, LocalString(ctx, params[1]), SOURCE2MOD_GEOIP_COUNTRY, params[2], params[3]);
        }

        // The *Ex / client variants name things in English; the player's
        // language is not known to the core.
        cell_t N_GeoipCountryEx(IPluginContext* ctx, const cell_t* params)
        {
            return Field(ctx, LocalString(ctx, params[1]), SOURCE2MOD_GEOIP_COUNTRY, params[2], params[3]);
        }

        cell_t N_GeoipContinent(IPluginContext* ctx, const cell_t* params)
        {
            return Field(ctx, LocalString(ctx, params[1]), SOURCE2MOD_GEOIP_CONTINENT, params[2], params[3]);
        }

        cell_t N_GeoipRegion(IPluginContext* ctx, const cell_t* params)
        {
            return Field(ctx, LocalString(ctx, params[1]), SOURCE2MOD_GEOIP_REGION, params[2], params[3]);
        }

        cell_t N_GeoipCity(IPluginContext* ctx, const cell_t* params)
        {
            return Field(ctx, LocalString(ctx, params[1]), SOURCE2MOD_GEOIP_CITY, params[2], params[3]);
        }

        cell_t N_GeoipTimezone(IPluginContext* ctx, const cell_t* params)
        {
            return Field(ctx, LocalString(ctx, params[1]), SOURCE2MOD_GEOIP_TIMEZONE, params[2], params[3]);
        }

        cell_t N_GeoipLatitude(IPluginContext* ctx, const cell_t* params)
        {
            double lat = 0.0, lon = 0.0;
            Geo().GetLocation(LocalString(ctx, params[1]), lat, lon);
            return sp_ftoc(static_cast<float>(lat));
        }

        cell_t N_GeoipLongitude(IPluginContext* ctx, const cell_t* params)
        {
            double lat = 0.0, lon = 0.0;
            Geo().GetLocation(LocalString(ctx, params[1]), lat, lon);
            return sp_ftoc(static_cast<float>(lon));
        }

        // native float GeoipDistance(float lat1, float lon1, float lat2, float lon2, int system = SYSTEM_METRIC);
        cell_t N_GeoipDistance(IPluginContext*, const cell_t* params)
        {
            constexpr double kPi = 3.14159265358979323846;
            const double lat1 = sp_ctof(params[1]) * kPi / 180.0, lon1 = sp_ctof(params[2]) * kPi / 180.0;
            const double lat2 = sp_ctof(params[3]) * kPi / 180.0, lon2 = sp_ctof(params[4]) * kPi / 180.0;
            const double a = std::pow(std::sin((lat2 - lat1) / 2), 2) + std::cos(lat1) * std::cos(lat2) * std::pow(std::sin((lon2 - lon1) / 2), 2);
            const double radius = params[5] == 1 ? 3958.8 : 6371.0;
            return sp_ftoc(static_cast<float>(2.0 * radius * std::asin(std::sqrt(a))));
        }

        const sp_nativeinfo_t g_geoipNatives[] = {
            { "Source2ModGeoIP.IsLoaded", N_GeoIsLoaded },
            { "Source2ModGeoIP.GetDatabaseType", N_GeoGetDatabaseType },
            { "Source2ModGeoIP.Lookup", N_GeoLookup },
            { "Source2ModGeoIP.LookupPlayer", N_GeoLookupPlayer },
            { "Source2ModGeoIP.GetLocation", N_GeoGetLocation },
            { "Source2ModGeoIP.LookupPath", N_GeoLookupPath },
            { "Source2ModGeoIP.GetPlayerIP", N_GeoGetPlayerIP },

            { "GeoipCode2", N_GeoipCode2 },
            { "GeoipRegionCode", N_GeoipRegionCode },
            { "GeoipContinentCode", N_GeoipContinentCode },
            { "GeoipCountry", N_GeoipCountry },
            { "GeoipCountryEx", N_GeoipCountryEx },
            { "GeoipContinent", N_GeoipContinent },
            { "GeoipRegion", N_GeoipRegion },
            { "GeoipCity", N_GeoipCity },
            { "GeoipTimezone", N_GeoipTimezone },
            { "GeoipLatitude", N_GeoipLatitude },
            { "GeoipLongitude", N_GeoipLongitude },
            { "GeoipDistance", N_GeoipDistance },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetGeoIPNatives()
    {
        return g_geoipNatives;
    }
}
