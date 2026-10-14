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
#include "raytrace.h"

#include "addresses.h"
#include "commands.h"
#include "core/scheduler.h"
#include "events.h"
#include "schema/navphysicsinterface.h"
#include "shared.h"
#include "utils/vectorextends.h"

#include <dynlibutils/module.hpp>
#include <engine2/inetworkserverservice.h>
#include <engine2/networkgameserverbase.h>
#include <source2mod/schema/entity/classes/CBaseEntity.h>
#include <source2mod/utils/plat.h>

namespace raytrace
{
    RayTrace rayTrace;

    TraceResult RayTrace::TraceShape(const Vector& vecStart, const QAngle& angAngles, CBaseEntity* pIgnoreEntity,
                                     TraceOptions* pTraceOptions)
    {
        CTraceFilterEx filter = pIgnoreEntity ? CTraceFilterEx(pIgnoreEntity) : CTraceFilterEx();

        filter.m_nInteractsAs = CONTENTS_EMPTY;
        filter.m_nInteractsWith = MASK_ALL;
        filter.m_nInteractsExclude = CONTENTS_EMPTY;

        if (pTraceOptions)
        {
            // Applied as given. Guarding each field against its own default
            // meant a caller could not ask for exactly that value -- there was
            // no way to request MASK_ALL for InteractsWith, or CONTENTS_EMPTY
            // for the other two -- and it made TraceOptions{} and nullptr mean
            // different things for no stated reason.
            filter.m_nInteractsAs = pTraceOptions->InteractsAs;
            filter.m_nInteractsWith = pTraceOptions->InteractsWith;
            filter.m_nInteractsExclude = pTraceOptions->InteractsExclude;
        }

        Vector forward;
        AngleVectors(angAngles, &forward);
        Vector vecEnd{
            vecStart.x + forward.x * 8192.f,
            vecStart.y + forward.y * 8192.f,
            vecStart.z + forward.z * 8192.f
        };

        Ray_t ray;

        return TraceShapeEx(vecStart, vecEnd, &filter, &ray);
    }

    TraceResult RayTrace::TraceEndShape(const Vector& vecStart, const Vector& vecEnd, CBaseEntity* pIgnoreEntity,
                                        TraceOptions* pTraceOptions)
    {
        CTraceFilterEx filter = pIgnoreEntity ? CTraceFilterEx(pIgnoreEntity) : CTraceFilterEx();

        filter.m_nInteractsAs = CONTENTS_EMPTY;
        filter.m_nInteractsWith = MASK_ALL;
        filter.m_nInteractsExclude = CONTENTS_EMPTY;

        if (pTraceOptions)
        {
            // Applied as given. Guarding each field against its own default
            // meant a caller could not ask for exactly that value -- there was
            // no way to request MASK_ALL for InteractsWith, or CONTENTS_EMPTY
            // for the other two -- and it made TraceOptions{} and nullptr mean
            // different things for no stated reason.
            filter.m_nInteractsAs = pTraceOptions->InteractsAs;
            filter.m_nInteractsWith = pTraceOptions->InteractsWith;
            filter.m_nInteractsExclude = pTraceOptions->InteractsExclude;
        }

        Ray_t ray;

        return TraceShapeEx(vecStart, vecEnd, &filter, &ray);
    }

    TraceResult RayTrace::TraceHullShape(const Vector& vecStart, const Vector& vecEnd, const Vector& vecMins,
                                         const Vector& vecMaxs, CBaseEntity* pIgnoreEntity, TraceOptions* pTraceOptions)
    {
        CTraceFilterEx filter = pIgnoreEntity ? CTraceFilterEx(pIgnoreEntity) : CTraceFilterEx();

        filter.m_nInteractsAs = CONTENTS_EMPTY;
        filter.m_nInteractsWith = MASK_ALL;
        filter.m_nInteractsExclude = CONTENTS_EMPTY;

        if (pTraceOptions)
        {
            // Applied as given. Guarding each field against its own default
            // meant a caller could not ask for exactly that value -- there was
            // no way to request MASK_ALL for InteractsWith, or CONTENTS_EMPTY
            // for the other two -- and it made TraceOptions{} and nullptr mean
            // different things for no stated reason.
            filter.m_nInteractsAs = pTraceOptions->InteractsAs;
            filter.m_nInteractsWith = pTraceOptions->InteractsWith;
            filter.m_nInteractsExclude = pTraceOptions->InteractsExclude;
        }

        Ray_t ray;
        ray.Init(vecMins, vecMaxs);

        return TraceShapeEx(vecStart, vecEnd, &filter, &ray);
    }

    TraceResult RayTrace::TraceShapeEx(const Vector& vecStart, const Vector& vecEnd, CTraceFilter* pTraceFilter,
                                       Ray_t* pRay)
    {
        if (!INavPhysicsInterface::Bind())
        {
            FP_ERROR("CNavPhysicsInterface vtable not found in the server module!");
            return TraceResult();
        }

        Vector vecStartCopy = vecStart;
        Vector vecEndCopy = vecEnd;
        CGameTrace trace;

        INavPhysicsInterface::TraceShape(*pRay, vecStartCopy, vecEndCopy, pTraceFilter, &trace);

        return TraceResult(&trace);
    }

    uint64 RayTrace::PointContents(const Vector* const vTestPos, uint64 nContentsMask)
    {
        if (!INavPhysicsInterface::Bind())
        {
            FP_ERROR("CNavPhysicsInterface vtable not found in the server module!");
            return 0;
        }

        return INavPhysicsInterface::PointContents(vTestPos, nContentsMask);
    }

    bool RayTrace::CheckAreaOverlappingEntity(const CCSNavArea* const rArea, const CBaseEntity* const rEntity,
                                              bool bExtrudeHullHeight)
    {
        if (!INavPhysicsInterface::Bind())
        {
            FP_ERROR("CNavPhysicsInterface vtable not found in the server module!");
            return false;
        }

        // The engine vtable takes it untyped; the type is for the plugin side.
        return INavPhysicsInterface::CheckAreaOverlappingEntity(rArea, rEntity, bExtrudeHullHeight);
    }

    void RayTrace::GetEntityWorldSpaceAABB(const CBaseEntity* const rEntity, Vector* pMinsOut, Vector* pMaxsOut)
    {
        if (!INavPhysicsInterface::Bind())
        {
            FP_ERROR("CNavPhysicsInterface vtable not found in the server module!");
            return;
        }

        INavPhysicsInterface::GetEntityWorldSpaceAABB(rEntity, pMinsOut, pMaxsOut);
    }
}
