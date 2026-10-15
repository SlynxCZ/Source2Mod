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

#include <source2mod/ISource2ModFeatures.h>

namespace features
{
    /// ISource2ModFeatures: what is there, asked of the systems that know --
    /// the SourcePawn native tables, the factory, the plugin libraries.
    class FeaturesManager : public ISource2ModFeatures
    {
    public:
        Source2ModFeatureStatus GetFeatureStatus(Source2ModFeatureType type, const char* name) override;
        bool IsNativeAvailable(const char* name) override;
        bool IsInterfaceAvailable(const char* iface) override;
        bool IsLibraryAvailable(const char* name) override;
        bool HasCapability(const char* name) override;

    private:
        Source2ModFeatureStatus NativeStatus(const char* name);
        Source2ModFeatureStatus CapabilityStatus(const char* name);
    };

    extern FeaturesManager featuresManager;
}
