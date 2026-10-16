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
 * SourcePawn natives for feature checks: SourceMod's GetFeatureStatus /
 * RequireFeature (source2mod.inc) and Source2ModFeatures
 * (source2mod/features.inc), both on ISource2ModFeatures.
 */
#include "natives.h"
#include "scriptplugin.h"

#include "core/features.h"
#include "core/pluginmanager.h"
#include "utils/log.h"

#include <string>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        bool TypeOf(IPluginContext* ctx, cell_t value, Source2ModFeatureType& type)
        {
            if (value < SOURCE2MOD_FEATURE_NATIVE || value > SOURCE2MOD_FEATURE_LIBRARY)
            {
                ctx->ThrowNativeError("Invalid feature type %d", value);
                return false;
            }
            type = static_cast<Source2ModFeatureType>(value);
            return true;
        }

        const char* TypeName(Source2ModFeatureType type)
        {
            switch (type)
            {
            case SOURCE2MOD_FEATURE_NATIVE:     return "Native";
            case SOURCE2MOD_FEATURE_CAPABILITY: return "Capability";
            case SOURCE2MOD_FEATURE_INTERFACE:  return "Interface";
            case SOURCE2MOD_FEATURE_LIBRARY:    return "Library";
            }
            return "Feature";
        }

        // native FeatureStatus GetFeatureStatus(FeatureType type, const char[] name);
        cell_t N_GetFeatureStatus(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModFeatureType type;
            if (!TypeOf(ctx, params[1], type))
                return SOURCE2MOD_FEATURE_UNKNOWN;
            return features::featuresManager.GetFeatureStatus(type, LocalString(ctx, params[2]));
        }

        cell_t N_FeaturesIsNativeAvailable(IPluginContext* ctx, const cell_t* params)
        {
            return features::featuresManager.IsNativeAvailable(LocalString(ctx, params[1]));
        }

        cell_t N_FeaturesIsInterfaceAvailable(IPluginContext* ctx, const cell_t* params)
        {
            return features::featuresManager.IsInterfaceAvailable(LocalString(ctx, params[1]));
        }

        cell_t N_FeaturesIsLibraryAvailable(IPluginContext* ctx, const cell_t* params)
        {
            return features::featuresManager.IsLibraryAvailable(LocalString(ctx, params[1]));
        }

        cell_t N_FeaturesHasCapability(IPluginContext* ctx, const cell_t* params)
        {
            return features::featuresManager.HasCapability(LocalString(ctx, params[1]));
        }

        const sp_nativeinfo_t g_featureNatives[] = {
            { "Source2ModFeatures.GetStatus", N_GetFeatureStatus },
            { "Source2ModFeatures.IsNativeAvailable", N_FeaturesIsNativeAvailable },
            { "Source2ModFeatures.IsInterfaceAvailable", N_FeaturesIsInterfaceAvailable },
            { "Source2ModFeatures.IsLibraryAvailable", N_FeaturesIsLibraryAvailable },
            { "Source2ModFeatures.HasCapability", N_FeaturesHasCapability },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetFeatureNatives()
    {
        return g_featureNatives;
    }
}
