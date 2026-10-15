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
#include "core/features.h"

#include "core/cookies.h"
#include "core/gamesystems.h"
#include "core/geoip.h"
#include "core/pluginapi.h"
#include "sourcepawn/natives.h"

#include <cstring>
#include <string>
#include <unordered_set>

namespace features
{
    FeaturesManager featuresManager;

    Source2ModFeatureStatus FeaturesManager::NativeStatus(const char* name)
    {
        // The core's table never changes after it is built.
        static const std::unordered_set<std::string> s_core = []
        {
            std::unordered_set<std::string> names;
            for (const sp_nativeinfo_t* n = sourcepawn::GetCoreNatives(); n->name; ++n)
                names.insert(n->name);
            return names;
        }();

        if (s_core.count(name))
            return SOURCE2MOD_FEATURE_AVAILABLE;

        switch (sourcepawn::pluginsys::NativeStatus(name))
        {
        case 1:  return SOURCE2MOD_FEATURE_AVAILABLE;
        case 0:  return SOURCE2MOD_FEATURE_UNAVAILABLE;
        default: return SOURCE2MOD_FEATURE_UNKNOWN;
        }
    }

    Source2ModFeatureStatus FeaturesManager::CapabilityStatus(const char* name)
    {
        bool available;
        if (!strcmp(name, SOURCE2MOD_CAPABILITY_SOURCEPAWN))
            available = true;
        else if (!strcmp(name, SOURCE2MOD_CAPABILITY_GEOIP))
            available = geoip::geoIpManager.IsLoaded();
        else if (!strcmp(name, SOURCE2MOD_CAPABILITY_CLIENTPREFS))
            available = cookies::cookiesManager.IsConnected();
        else if (!strcmp(name, SOURCE2MOD_CAPABILITY_GAMESYSTEMS))
            available = gamesystems::gameSystemsManager.IsAvailable();
        else
            return SOURCE2MOD_FEATURE_UNKNOWN;

        return available ? SOURCE2MOD_FEATURE_AVAILABLE : SOURCE2MOD_FEATURE_UNAVAILABLE;
    }

    Source2ModFeatureStatus FeaturesManager::GetFeatureStatus(Source2ModFeatureType type, const char* name)
    {
        if (!name || !*name)
            return SOURCE2MOD_FEATURE_UNKNOWN;

        switch (type)
        {
        case SOURCE2MOD_FEATURE_NATIVE:
            return NativeStatus(name);
        case SOURCE2MOD_FEATURE_CAPABILITY:
            return CapabilityStatus(name);
        case SOURCE2MOD_FEATURE_INTERFACE:
            // A name the factory does not know is as unknown as one that
            // never existed; there is no telling the two apart from here.
            return PluginApi::IsInterfaceServed(name) ? SOURCE2MOD_FEATURE_AVAILABLE : SOURCE2MOD_FEATURE_UNKNOWN;
        case SOURCE2MOD_FEATURE_LIBRARY:
            return sourcepawn::pluginsys::LibraryAvailable(name) ? SOURCE2MOD_FEATURE_AVAILABLE : SOURCE2MOD_FEATURE_UNKNOWN;
        }
        return SOURCE2MOD_FEATURE_UNKNOWN;
    }

    bool FeaturesManager::IsNativeAvailable(const char* name)
    {
        return GetFeatureStatus(SOURCE2MOD_FEATURE_NATIVE, name) == SOURCE2MOD_FEATURE_AVAILABLE;
    }

    bool FeaturesManager::IsInterfaceAvailable(const char* iface)
    {
        return GetFeatureStatus(SOURCE2MOD_FEATURE_INTERFACE, iface) == SOURCE2MOD_FEATURE_AVAILABLE;
    }

    bool FeaturesManager::IsLibraryAvailable(const char* name)
    {
        return GetFeatureStatus(SOURCE2MOD_FEATURE_LIBRARY, name) == SOURCE2MOD_FEATURE_AVAILABLE;
    }

    bool FeaturesManager::HasCapability(const char* name)
    {
        return GetFeatureStatus(SOURCE2MOD_FEATURE_CAPABILITY, name) == SOURCE2MOD_FEATURE_AVAILABLE;
    }
}
