/**
* vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * Copyright (C) 2025-2026 Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl.
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
 * As a special exception, Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
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
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */
#pragma once
#include <dynlibutils/memaddr.hpp>
#include <engine2/isource2server.h>
#include <engine2/ivengineserver2.h>
#include <entitysystem.h>
#include <source2mod/ISource2ModTrace.h>
#include <source2mod/schema/entity/classes/CBaseEntity.h>
#include <source2mod/schema/entity/classes/CCollisionProperty.h>

// CTraceFilterEx now lives in the SDK (ISource2ModTrace.h) so plugins calling
// TraceShapeEx can build one too.

namespace raytrace
{
    class RayTrace : public ISource2ModTrace
    {
    public:
        TraceResult TraceShape(const Vector& vecStart, const QAngle& angAngles, CBaseEntity* pIgnoreEntity, TraceOptions* pTraceOptions) override;
        TraceResult TraceEndShape(const Vector& vecStart, const Vector& vecEnd, CBaseEntity* pIgnoreEntity, TraceOptions* pTraceOptions) override;
        TraceResult TraceHullShape(const Vector& vecStart, const Vector& vecEnd, const Vector& vecMins, const Vector& vecMaxs, CBaseEntity* pIgnoreEntity, TraceOptions* pTraceOptions) override;
        TraceResult TraceShapeEx(const Vector& vecStart, const Vector& vecEnd, CTraceFilter* pTraceFilter, Ray_t* pRay) override;
        uint64 PointContents(const Vector* const vTestPos, uint64 nContentsMask) override;
        bool CheckAreaOverlappingEntity(const CCSNavArea* const rArea, const CBaseEntity* const rEntity, bool bExtrudeHullHeight) override;
        void GetEntityWorldSpaceAABB(const CBaseEntity* const rEntity, Vector* pMinsOut, Vector* pMaxsOut) override;
    };

    extern RayTrace rayTrace;
}
