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
 * SourcePawn natives for GeoIP: Source2ModGeoIP (source2mod/geoip.inc) and
 * SourceMod's geoip.inc (GeoipCode2, GeoipCountry, GeoipCity, ...), both on
 * ISource2ModGeoIP. GeoipCode3 is not there: GeoLite2 has no three-letter codes.
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

        const sp_nativeinfo_t g_geoipNatives[] = {
            { "Source2ModGeoIP.IsLoaded", N_GeoIsLoaded },
            { "Source2ModGeoIP.GetDatabaseType", N_GeoGetDatabaseType },
            { "Source2ModGeoIP.Lookup", N_GeoLookup },
            { "Source2ModGeoIP.LookupPlayer", N_GeoLookupPlayer },
            { "Source2ModGeoIP.GetLocation", N_GeoGetLocation },
            { "Source2ModGeoIP.LookupPath", N_GeoLookupPath },
            { "Source2ModGeoIP.GetPlayerIP", N_GeoGetPlayerIP },

            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetGeoIPNatives()
    {
        return g_geoipNatives;
    }
}
