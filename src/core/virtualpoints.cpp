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
 * As a special exception, Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl gives
 * you permission to link the code of this program (as well as its derivative
 * works) to "Counter-Strike 2," "Source 2," "Steam," and any Game MODs or
 * server software running on software by Valve Corporation. You must obey the
 * GNU General Public License in all respects for all other code used.
 *
 * Additionally, this exception applies to all derivative works unless
 * otherwise stated in LICENSE.txt.
 *
 * Authors:
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */
#include "virtualpoints.h"

#include "dynamichooks.h"
#include "functions.h"
#include "hookid.h"
#include "utils/log.h"

#include <type_traits>

namespace virtualpoints
{
    namespace
    {
        struct Def
        {
            HookPointId point;
            const char* name;
            const char* gamedata;       // an offsets entry with a "function" object
            const char* defaultClass;   // what HookTarget::All() means; nullptr: refused
        };

        // A class whose objects all share one vtable may take All(); an entity
        // virtual has an override per class, so it needs Instance or Class.
        constexpr Def kDefs[] = {
            { HookPointId::Spawn, "Spawn", "CBaseEntity::Spawn", nullptr },
            { HookPointId::Use, "Use", "CBaseEntity::Use", nullptr },
            { HookPointId::StartTouch, "StartTouch", "CBaseEntity::StartTouch", nullptr },
            { HookPointId::EndTouch, "EndTouch", "CBaseEntity::EndTouch", nullptr },
            { HookPointId::Blocked, "Blocked", "CBaseEntity::Blocked", nullptr },
            { HookPointId::Think, "Think", "CBaseEntity::Think", nullptr },
            { HookPointId::Killed, "Killed", "CBaseEntity::Event_Killed", nullptr },
            { HookPointId::WeaponPickup, "WeaponPickup", "CCSPlayer_WeaponServices::BumpWeapon", "CCSPlayer_WeaponServices" },
            { HookPointId::WeaponSwitch, "WeaponSwitch", "CCSPlayer_WeaponServices::SelectItem", "CCSPlayer_WeaponServices" },
        };

        const Def* DefOf(HookPointId point)
        {
            for (const Def& d : kDefs)
                if (d.point == point)
                    return &d;
            return nullptr;
        }

        // The context of each point, read off the call (`this` is argument 0).
        SpawnContext Make(FunctionContext& f, SpawnContext*)
        {
            return { f.GetArg<CBaseEntity*>(0), f.GetArg<const CEntityKeyValues*>(1) };
        }
        UseContext Make(FunctionContext& f, UseContext*)
        {
            return { f.GetArg<CBaseEntity*>(0), f.GetArg<EntityUseInfo*>(1) };
        }
        EntityOtherContext Make(FunctionContext& f, EntityOtherContext*)
        {
            return { f.GetArg<CBaseEntity*>(0), f.GetArg<CBaseEntity*>(1) };
        }
        ThinkContext Make(FunctionContext& f, ThinkContext*)
        {
            return { f.GetArg<CBaseEntity*>(0) };
        }
        KilledContext Make(FunctionContext& f, KilledContext*)
        {
            return { f.GetArg<CBaseEntity*>(0), f.GetArg<const CTakeDamageResult*>(1), false };
        }
        WeaponPickupContext Make(FunctionContext& f, WeaponPickupContext*)
        {
            return { f.GetArg<CCSPlayer_WeaponServices*>(0), f.GetArg<CBasePlayerWeapon*>(1), 0 };
        }
        WeaponSwitchContext Make(FunctionContext& f, WeaponSwitchContext*)
        {
            return { f.GetArg<CCSPlayer_WeaponServices*>(0), f.GetArg<CBasePlayerWeapon*>(1), f.GetArg<std::int32_t>(2), false };
        }

        template <typename R>
        R CallOriginalOf(const void* data)
        {
            return static_cast<FunctionContext*>(const_cast<void*>(data))->CallOriginal<R>();
        }

        // The typed handler as a Function-point handler: the context from the
        // call, the answer's value (or ctx.result) as the return value.
        template <typename CONTEXT>
        HookHandler<FunctionContext> Adapt(const void* handler)
        {
            HookHandler<CONTEXT> typed = *static_cast<const HookHandler<CONTEXT>*>(handler);
            return HookHandler<FunctionContext>([typed](FunctionContext& f, bool post) -> HookReturn<void>
            {
                using R = typename CONTEXT::Return;
                CONTEXT ctx = Make(f, static_cast<CONTEXT*>(nullptr));
                ctx.callOriginal_ = &CallOriginalOf<R>;
                ctx.callOriginalData_ = &f;
                if constexpr (!std::is_void_v<R>)
                {
                    if (post)
                        ctx.result = f.GetReturn<R>();
                }

                const HookReturn<R> answer = typed(ctx, post);
                if constexpr (!std::is_void_v<R>)
                {
                    if (answer.action != Action::Ignore)
                        f.SetReturn<R>(answer.hasValue ? answer.value : ctx.result);
                }
                return HookReturn<void>(answer.action);
            });
        }

        template <typename CONTEXT>
        PluginId OwnerOfHandler(const void* handler)
        {
            return hookid::OwnerOfHandler(*static_cast<const HookHandler<CONTEXT>*>(handler));
        }

        // Calls visit(Context*) with the point's context type.
        template <typename VISIT>
        auto WithContext(HookPointId point, VISIT&& visit)
        {
            switch (point)
            {
            case HookPointId::Spawn: return visit(static_cast<SpawnContext*>(nullptr));
            case HookPointId::Use: return visit(static_cast<UseContext*>(nullptr));
            case HookPointId::StartTouch:
            case HookPointId::EndTouch:
            case HookPointId::Blocked: return visit(static_cast<EntityOtherContext*>(nullptr));
            case HookPointId::Think: return visit(static_cast<ThinkContext*>(nullptr));
            case HookPointId::Killed: return visit(static_cast<KilledContext*>(nullptr));
            case HookPointId::WeaponPickup: return visit(static_cast<WeaponPickupContext*>(nullptr));
            default: return visit(static_cast<WeaponSwitchContext*>(nullptr));
            }
        }

        ISource2ModFunction* FunctionOf(const Def& def)
        {
            // Find keeps the described function for good: one per entry.
            return functions::functionsManager.Find(def.gamedata);
        }
    }

    bool Is(HookPointId point)
    {
        return DefOf(point) != nullptr;
    }

    bool IsAvailable(HookPointId point)
    {
        const Def* def = DefOf(point);
        return def && FunctionOf(*def);
    }

    const char* Name(HookPointId point)
    {
        const Def* def = DefOf(point);
        return def ? def->name : "?";
    }

    PluginId OwnerOf(HookPointId point, const void* handler)
    {
        if (!Is(point) || !handler)
            return 0;
        return WithContext(point, [handler](auto* tag) { return OwnerOfHandler<std::remove_pointer_t<decltype(tag)>>(handler); });
    }

    Source2ModHookId Add(HookPointId point, PluginId owner, const void* handler, bool post, const HookTarget& target,
                         const HookFilter& filter)
    {
        const Def* def = DefOf(point);
        if (!def || !handler)
            return 0;

        ISource2ModFunction* function = FunctionOf(*def);
        if (!function)
        {
            FP_WARN("Hook on {}: gamedata entry '{}' is missing or does not resolve on this platform", def->name, def->gamedata);
            return 0;
        }

        HookTarget where = target;
        if (where.kind == HookTarget::Kind::All)
        {
            if (!def->defaultClass)
            {
                FP_WARN("Hook on {}: every entity class has its own {} -- hook it on HookTarget::Instance(entity) "
                        "or HookTarget::Class(\"CCSPlayerPawn\"), not All()", def->name, def->name);
                return 0;
            }
            where = HookTarget::Class(def->defaultClass);
        }

        HookHandler<FunctionContext> adapted =
            WithContext(point, [handler](auto* tag) { return Adapt<std::remove_pointer_t<decltype(tag)>>(handler); });
        return dynamichooks::dynamicHooksManager.Add(function, owner, std::move(adapted), post, where, filter);
    }
}
