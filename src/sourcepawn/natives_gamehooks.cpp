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
 * Game hooks and transmit of the SourcePawn API mirroring the C++ one
 * (source2mod/gamehooks.inc, source2mod/transmit.inc), on the core's game
 * hooks and transmit managers.
 *
 * A handler's context (TakeDamageContext, MovementContext, ..., and what
 * it lends: the damage info, the move data; the CheckTransmit viewer list)
 * is a handle made for the call and freed when the handler returns. The game hooks manager tells owners apart
 * by the handler's code, which for a script is the core's; the hooks a
 * script made are therefore remembered here and taken down when it unloads.
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/gamehooks.h"
#include "core/transmit.h"

#include <source2mod/schema/movedata.h>
#include <source2mod/schema/takedamageinfo.h>
#include <source2mod/schema/takedamageresult.h>
#include <source2mod/schema/entity/classes/CBaseEntity.h>
#include <source2mod/schema/entity/classes/CBasePlayerController.h>
#include <source2mod/schema/entity/classes/CBasePlayerWeapon.h>
#include <source2mod/schema/entity/classes/CCSPlayerController.h>
#include <source2mod/schema/entity/classes/CCSPlayerPawn.h>
#include <source2mod/schema/entity/classes/CCSPlayerPawnBase.h>
#include <source2mod/schema/entity/classes/CCSPlayer_ItemServices.h>
#include <source2mod/schema/entity/classes/CCSPlayer_MovementServices.h>
#include <source2mod/schema/entity/classes/CCSPlayer_WeaponServices.h>
#include <entity2/entityidentity.h>

#include <sp_callable.h>

#include <algorithm>
#include <map>
#include <type_traits>
#include <vector>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        PluginId CallerId(IPluginContext* ctx)
        {
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            return plugin ? plugin->Id() : 0;
        }

        IPluginFunction* Function(IPluginContext* ctx, cell_t id)
        {
            IPluginFunction* fn = ctx->GetFunctionById(static_cast<funcid_t>(id));
            if (!fn)
                ctx->ThrowNativeError("Invalid function id (%X)", id);
            return fn;
        }

        CEntityInstance* EntityAt(IPluginContext* ctx, cell_t entity)
        {
            return EntityArg(ctx, entity);
        }

        Action ToAction(cell_t res)
        {
            return static_cast<Action>(std::clamp<cell_t>(res, 0, 2));
        }

        HandleType FrameType(const char* name)
        {
            return Handles().CreateType(name, nullptr, 0, false);
        }

        // ---- game hooks ---------------------------------------------------------------

        // One call of a handler. The context handle points at it; the objects
        // the script reaches through the context (the move data, the damage
        // info) get handles made on first use and freed with the frame. Frames
        // nest -- a handler can deal damage and run TakeDamage's hooks -- and
        // a lent object goes to the innermost one.
        struct Frame
        {
            void* ctx;
            PluginId owner;
            struct Lent
            {
                void* object;
                HandleType type;
                cell_t handle;
            };
            std::vector<Lent> lent;
        };
        std::vector<Frame*> g_frames;

        cell_t Lend(HandleType type, void* object)
        {
            if (!object || g_frames.empty())
                return 0;
            Frame* f = g_frames.back();
            for (const Frame::Lent& l : f->lent)
                if (l.object == object && l.type == type)
                    return l.handle;
            const cell_t h = Handles().Create(type, object, f->owner);
            if (h)
                f->lent.push_back({ object, type, h });
            return h;
        }

        HandleType DamageInfoType()
        {
            static const HandleType t = FrameType("CTakeDamageInfo");
            return t;
        }

        HandleType DamageResultType()
        {
            static const HandleType t = FrameType("CTakeDamageResult");
            return t;
        }

        HandleType MoveDataType()
        {
            static const HandleType t = FrameType("CMoveData");
            return t;
        }

        // What each context is to SourcePawn: its handle type's name and how
        // the result goes to and comes back from the `&result` argument
        // (none for void functions and ProcessUsercmds' opaque pointer).
        template <typename C>
        struct CtxTraits;

#define S2M_CTX(C, BYREF)                                                       \
        template <>                                                             \
        struct CtxTraits<C>                                                     \
        {                                                                       \
            static constexpr const char* name = #C;                             \
            static constexpr bool byRef = BYREF;                                \
        };
        S2M_CTX(TakeDamageContext, true)
        S2M_CTX(CanAcquireContext, true)
        S2M_CTX(CanMoveContext, true)
        S2M_CTX(CanUseContext, true)
        S2M_CTX(PostThinkContext, false)
        S2M_CTX(ProcessUsercmdsContext, false)
        S2M_CTX(SimulateUserCommandsContext, false)
        S2M_CTX(RunCommandContext, false)
        S2M_CTX(AcceptInputContext, true)
        S2M_CTX(TouchContext, false)
        S2M_CTX(DropWeaponContext, false)
        S2M_CTX(MovementContext, true)
        S2M_CTX(AirAccelerateContext, false)
        S2M_CTX(GroundAccelerateContext, false)
        S2M_CTX(CategorizePositionContext, false)
        S2M_CTX(CheckVelocityContext, false)
        S2M_CTX(FullWalkMoveContext, false)
        S2M_CTX(SetupMoveContext, false)
        S2M_CTX(TryPlayerMoveContext, false)
        S2M_CTX(LegacyJumpContext, false)
        S2M_CTX(ModernJumpContext, false)
#undef S2M_CTX

        template <typename C>
        HandleType CtxType()
        {
            static const HandleType t = FrameType(CtxTraits<C>::name);
            return t;
        }

        template <typename C>
        GameHookReturn<typename C::Return> RunHandler(IPluginFunction* fn, PluginId owner, C& c, bool post)
        {
            using R = typename C::Return;
            Frame frame{ &c, owner, {} };
            g_frames.push_back(&frame);
            const cell_t handle = Handles().Create(CtxType<C>(), &frame, owner);

            sp::CallArgs args;
            args.PushCell(handle);
            args.PushCell(post ? 1 : 0);
            cell_t result = 0;
            if constexpr (CtxTraits<C>::byRef)
            {
                result = static_cast<cell_t>(c.result);
                args.PushCellByRef(&result);
            }
            cell_t res = 0;
            Invoke(fn, args, &res);

            g_frames.pop_back();
            for (const Frame::Lent& l : frame.lent)
                Handles().Free(l.handle, 0, true);
            Handles().Free(handle, 0, true);

            const Action action = ToAction(res);
            if constexpr (std::is_void_v<R>)
                return action;
            else if constexpr (CtxTraits<C>::byRef)
            {
                if (action == Action::Ignore)
                    return action;
                if constexpr (std::is_same_v<R, bool>)
                    return GameHookReturn<R>(action, result != 0);
                else
                    return GameHookReturn<R>(action, static_cast<R>(result));
            }
            else
                return action;      // the value stays what ctx.result holds
        }

        struct ScriptGameHook
        {
            PluginId owner;
            bool (*unhook)(Source2ModGameHookId);
        };
        std::map<Source2ModGameHookId, ScriptGameHook> g_gameHooks;

        ISource2ModGameHooks& GH()
        {
            return gamehooks::gameHooksManager;
        }

        template <typename C, Source2ModGameHookId (ISource2ModGameHooks::*HOOK)(GameHookHandler<C>, bool), bool (ISource2ModGameHooks::*UNHOOK)(Source2ModGameHookId)>
        cell_t N_Hook(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[1]);
            if (!fn)
                return 0;
            const PluginId owner = CallerId(ctx);
            GameHookHandler<C> handler([fn, owner](C& c, bool post) -> GameHookReturn<typename C::Return>
            {
                return RunHandler(fn, owner, c, post);
            });
            const Source2ModGameHookId id = (GH().*HOOK)(std::move(handler), params[2] != 0);
            if (id)
                g_gameHooks[id] = { owner, [](Source2ModGameHookId i) { return (GH().*UNHOOK)(i); } };
            return id;
        }

        bool Unhook(Source2ModGameHookId id)
        {
            auto it = g_gameHooks.find(id);
            if (it == g_gameHooks.end())
                return false;
            auto unhook = it->second.unhook;
            g_gameHooks.erase(it);
            return unhook(id);
        }

        cell_t N_UnhookGameHook(IPluginContext* ctx, const cell_t* params)
        {
            auto it = g_gameHooks.find(params[1]);
            if (it == g_gameHooks.end() || it->second.owner != CallerId(ctx))
                return 0;
            return Unhook(params[1]);
        }

        cell_t N_IsAvailable(IPluginContext* ctx, const cell_t* params)
        {
            if (params[1] < 0 || params[1] >= static_cast<cell_t>(GameHook::Count))
                return ctx->ThrowNativeError("Invalid game hook %d", params[1]);
            return GH().IsAvailable(static_cast<GameHook>(params[1]));
        }

        template <typename C>
        C* Ctx(IPluginContext* ctx, cell_t handle)
        {
            auto* f = static_cast<Frame*>(ReadHandle(ctx, handle, CtxType<C>()));
            return f ? static_cast<C*>(f->ctx) : nullptr;
        }

        template <typename C>
        cell_t N_CallOriginal(IPluginContext* ctx, const cell_t* params)
        {
            C* c = Ctx<C>(ctx, params[1]);
            if (!c)
                return 0;
            using R = typename C::Return;
            if constexpr (std::is_void_v<R>)
            {
                c->CallOriginal();
                return 0;
            }
            else if constexpr (std::is_pointer_v<R>)
                return c->CallOriginal() != nullptr;
            else
                return static_cast<cell_t>(c->CallOriginal());
        }

        cell_t VecOut(IPluginContext* ctx, cell_t addr, const float* v, int n = 3)
        {
            cell_t* out;
            ctx->LocalToPhysAddr(addr, &out);
            for (int i = 0; i < 3; i++)
                out[i] = i < n ? sp_ftoc(v[i]) : sp_ftoc(0.0f);
            return 1;
        }

        cell_t VecIn(IPluginContext* ctx, cell_t addr, float* v, int n = 3)
        {
            cell_t* in;
            ctx->LocalToPhysAddr(addr, &in);
            for (int i = 0; i < n; i++)
                v[i] = sp_ctof(in[i]);
            return 1;
        }

        CCSPlayerPawn* PawnOf(CPlayerPawnComponent* services)
        {
            return services ? services->GetPlayerPawn() : nullptr;
        }

        // A context property: an entity, a SchemaObject, a cell.
#define S2M_CTX_ENTITY(C, prop, expr)                                           \
        cell_t N_##C##_##prop(IPluginContext* ctx, const cell_t* params)        \
        {                                                                       \
            C* c = Ctx<C>(ctx, params[1]);                                      \
            return c ? EntityCell(expr) : -1;                                      \
        }
#define S2M_CTX_OBJECT(C, prop, expr)                                           \
        cell_t N_##C##_##prop(IPluginContext* ctx, const cell_t* params)        \
        {                                                                       \
            C* c = Ctx<C>(ctx, params[1]);                                      \
            return c ? SchemaObjectForPointer(expr) : 0;                        \
        }
#define S2M_CTX_LENT(C, prop, type, expr)                                       \
        cell_t N_##C##_##prop(IPluginContext* ctx, const cell_t* params)        \
        {                                                                       \
            C* c = Ctx<C>(ctx, params[1]);                                      \
            return c ? Lend(type(), expr) : 0;                                  \
        }
#define S2M_CTX_CELL(C, prop, expr)                                             \
        cell_t N_##C##_##prop(IPluginContext* ctx, const cell_t* params)        \
        {                                                                       \
            C* c = Ctx<C>(ctx, params[1]);                                      \
            return c ? static_cast<cell_t>(expr) : 0;                           \
        }
#define S2M_CTX_FLOAT(C, prop, expr)                                            \
        cell_t N_##C##_##prop(IPluginContext* ctx, const cell_t* params)        \
        {                                                                       \
            C* c = Ctx<C>(ctx, params[1]);                                      \
            return c ? sp_ftoc(expr) : 0;                                       \
        }
// The movement contexts' shared trio.
#define S2M_CTX_MOVEMENT(C)                                                     \
        S2M_CTX_OBJECT(C, services, c->services)                                \
        S2M_CTX_ENTITY(C, pawn, PawnOf(c->services))                            \
        S2M_CTX_LENT(C, moveData, MoveDataType, c->moveData)

        S2M_CTX_ENTITY(TakeDamageContext, entity, c->entity)
        S2M_CTX_LENT(TakeDamageContext, info, DamageInfoType, c->info)
        S2M_CTX_LENT(TakeDamageContext, damageResult, DamageResultType, c->damageResult)

        S2M_CTX_OBJECT(CanAcquireContext, services, c->services)
        S2M_CTX_ENTITY(CanAcquireContext, pawn, PawnOf(c->services))
        S2M_CTX_OBJECT(CanAcquireContext, item, c->item)
        S2M_CTX_CELL(CanAcquireContext, method, c->method)

        S2M_CTX_ENTITY(CanMoveContext, pawn, c->pawn)

        S2M_CTX_OBJECT(CanUseContext, services, c->services)
        S2M_CTX_ENTITY(CanUseContext, pawn, PawnOf(c->services))
        S2M_CTX_ENTITY(CanUseContext, weapon, c->weapon)

        S2M_CTX_ENTITY(PostThinkContext, pawn, c->pawn)

        S2M_CTX_ENTITY(ProcessUsercmdsContext, controller, c->controller)
        S2M_CTX_OBJECT(ProcessUsercmdsContext, cmds, c->cmds)
        S2M_CTX_CELL(ProcessUsercmdsContext, count, c->count)
        S2M_CTX_CELL(ProcessUsercmdsContext, paused, c->paused)
        S2M_CTX_FLOAT(ProcessUsercmdsContext, margin, c->margin)

        S2M_CTX_ENTITY(SimulateUserCommandsContext, controller, c->controller)

        S2M_CTX_OBJECT(RunCommandContext, services, c->services)
        S2M_CTX_ENTITY(RunCommandContext, pawn, PawnOf(c->services))
        S2M_CTX_OBJECT(RunCommandContext, cmd, c->cmd)

        S2M_CTX_ENTITY(AcceptInputContext, entity, c->identity ? c->identity->m_pInstance : nullptr)
        S2M_CTX_ENTITY(AcceptInputContext, activator, c->activator)
        S2M_CTX_ENTITY(AcceptInputContext, caller, c->caller)

        S2M_CTX_ENTITY(TouchContext, entity, c->entity)
        S2M_CTX_ENTITY(TouchContext, other, c->other)

        S2M_CTX_OBJECT(DropWeaponContext, services, c->services)
        S2M_CTX_ENTITY(DropWeaponContext, pawn, PawnOf(c->services))
        S2M_CTX_ENTITY(DropWeaponContext, weapon, c->weapon)

        S2M_CTX_MOVEMENT(MovementContext)

        S2M_CTX_MOVEMENT(AirAccelerateContext)
        S2M_CTX_FLOAT(AirAccelerateContext, wishSpeed, c->wishSpeed)
        S2M_CTX_FLOAT(AirAccelerateContext, acceleration, c->acceleration)

        S2M_CTX_MOVEMENT(GroundAccelerateContext)
        S2M_CTX_FLOAT(GroundAccelerateContext, frameTime, c->frameTime)
        S2M_CTX_FLOAT(GroundAccelerateContext, wishSpeed, c->wishSpeed)
        S2M_CTX_FLOAT(GroundAccelerateContext, acceleration, c->acceleration)

        S2M_CTX_MOVEMENT(CategorizePositionContext)
        S2M_CTX_CELL(CategorizePositionContext, stayOnGround, c->stayOnGround)

        S2M_CTX_MOVEMENT(CheckVelocityContext)

        S2M_CTX_MOVEMENT(FullWalkMoveContext)
        S2M_CTX_CELL(FullWalkMoveContext, onGround, c->onGround)

        S2M_CTX_MOVEMENT(SetupMoveContext)
        S2M_CTX_OBJECT(SetupMoveContext, cmd, c->cmd)

        S2M_CTX_MOVEMENT(TryPlayerMoveContext)

        S2M_CTX_OBJECT(LegacyJumpContext, jump, c->jump)
        S2M_CTX_LENT(LegacyJumpContext, moveData, MoveDataType, c->moveData)
        S2M_CTX_OBJECT(ModernJumpContext, jump, c->jump)
        S2M_CTX_LENT(ModernJumpContext, moveData, MoveDataType, c->moveData)
#undef S2M_CTX_MOVEMENT
#undef S2M_CTX_FLOAT
#undef S2M_CTX_CELL
#undef S2M_CTX_LENT
#undef S2M_CTX_OBJECT
#undef S2M_CTX_ENTITY

        // A Vector* argument: get / set, false when the game passed none.
#define S2M_CTX_VECPTR(C, Name, member)                                         \
        cell_t N_##C##_Get##Name(IPluginContext* ctx, const cell_t* params)     \
        {                                                                       \
            C* c = Ctx<C>(ctx, params[1]);                                      \
            return c && c->member ? VecOut(ctx, params[2], &c->member->x) : 0;  \
        }                                                                       \
        cell_t N_##C##_Set##Name(IPluginContext* ctx, const cell_t* params)     \
        {                                                                       \
            C* c = Ctx<C>(ctx, params[1]);                                      \
            return c && c->member ? VecIn(ctx, params[2], &c->member->x) : 0;   \
        }
        S2M_CTX_VECPTR(DropWeaponContext, Target, target)
        S2M_CTX_VECPTR(DropWeaponContext, Velocity, velocity)
        S2M_CTX_VECPTR(AirAccelerateContext, WishDirection, wishDirection)
        S2M_CTX_VECPTR(GroundAccelerateContext, WishDirection, wishDirection)
        S2M_CTX_VECPTR(TryPlayerMoveContext, FirstDest, firstDest)
#undef S2M_CTX_VECPTR

        cell_t N_AcceptInputContext_GetInputName(IPluginContext* ctx, const cell_t* params)
        {
            AcceptInputContext* c = Ctx<AcceptInputContext>(ctx, params[1]);
            const char* name = c && c->inputName ? c->inputName->String() : "";
            size_t written = 0;
            ctx->StringToLocalUTF8(params[2], params[3], name ? name : "", &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_AcceptInputContext_GetValue(IPluginContext* ctx, const cell_t* params)
        {
            AcceptInputContext* c = Ctx<AcceptInputContext>(ctx, params[1]);
            const char* value = c && c->value ? c->value->ToString() : "";
            size_t written = 0;
            ctx->StringToLocalUTF8(params[2], params[3], value ? value : "", &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_AcceptInputContext_SetValue(IPluginContext* ctx, const cell_t* params)
        {
            AcceptInputContext* c = Ctx<AcceptInputContext>(ctx, params[1]);
            if (!c || !c->value)
                return 0;
            *c->value = variant_t(LocalString(ctx, params[2]), true);
            return 1;
        }

        cell_t N_TryPlayerMoveContext_GetFirstTrace(IPluginContext* ctx, const cell_t* params)
        {
            TryPlayerMoveContext* c = Ctx<TryPlayerMoveContext>(ctx, params[1]);
            return c && c->firstTrace ? NewTraceResult(ctx, c->firstTrace) : 0;
        }

        cell_t N_TryPlayerMoveContext_GetIsSurfing(IPluginContext* ctx, const cell_t* params)
        {
            TryPlayerMoveContext* c = Ctx<TryPlayerMoveContext>(ctx, params[1]);
            return c && c->isSurfing && *c->isSurfing;
        }

        cell_t N_TryPlayerMoveContext_SetIsSurfing(IPluginContext* ctx, const cell_t* params)
        {
            TryPlayerMoveContext* c = Ctx<TryPlayerMoveContext>(ctx, params[1]);
            if (!c || !c->isSurfing)
                return 0;
            *c->isSurfing = params[2] != 0;
            return 1;
        }

        // ---- CTakeDamageInfo / CTakeDamageResult / CMoveData fields -----------------

        template <typename T>
        T* Lent(IPluginContext* ctx, cell_t handle, HandleType type)
        {
            return static_cast<T*>(ReadHandle(ctx, handle, type));
        }

        void WriteMask64(IPluginContext* ctx, cell_t addr, uint64_t v)
        {
            cell_t* out;
            ctx->LocalToPhysAddr(addr, &out);
            out[0] = static_cast<cell_t>(v & 0xFFFFFFFFu);
            out[1] = static_cast<cell_t>(v >> 32);
        }

        uint64_t ReadMask64(IPluginContext* ctx, cell_t addr)
        {
            cell_t* in;
            ctx->LocalToPhysAddr(addr, &in);
            return static_cast<uint32_t>(in[0]) | (static_cast<uint64_t>(static_cast<uint32_t>(in[1])) << 32);
        }

#define S2M_FIELD_GET(T, TYPE, field, conv)                                      \
        cell_t N_##T##_get_##field(IPluginContext* ctx, const cell_t* params)    \
        {                                                                        \
            T* o = Lent<T>(ctx, params[1], TYPE());                              \
            return o ? conv(o->field) : 0;                                       \
        }
#define S2M_FIELD_SET(T, TYPE, field, conv)                                      \
        cell_t N_##T##_set_##field(IPluginContext* ctx, const cell_t* params)    \
        {                                                                        \
            if (T* o = Lent<T>(ctx, params[1], TYPE()))                          \
                o->field = conv(params[2]);                                      \
            return 0;                                                            \
        }
#define S2M_FIELD_VEC(T, TYPE, field, N)                                         \
        cell_t N_##T##_##field(IPluginContext* ctx, const cell_t* params)        \
        {                                                                        \
            T* o = Lent<T>(ctx, params[1], TYPE());                              \
            return o ? VecOut(ctx, params[2], &o->field.x, N) : 0;               \
        }                                                                        \
        cell_t N_##T##_Set_##field(IPluginContext* ctx, const cell_t* params)    \
        {                                                                        \
            T* o = Lent<T>(ctx, params[1], TYPE());                              \
            return o ? VecIn(ctx, params[2], &o->field.x, N) : 0;                \
        }
#define S2M_CELL(v) static_cast<cell_t>(v)
#define S2M_BOOL(v) ((v) != 0)
#define S2M_ENTITY(v) EntityCell((v).Get())
#define S2M_HANDLE(v) CHandle<CBaseEntity>(static_cast<CBaseEntity*>(OptionalEntityAt(ctx, v)))

        CEntityInstance* OptionalEntityAt(IPluginContext* ctx, cell_t index)
        {
            return index == -1 ? nullptr : EntityAt(ctx, index);
        }

        // CTakeDamageInfo
#define S2M_INFO_FLOAT(f) S2M_FIELD_GET(CTakeDamageInfo, DamageInfoType, f, sp_ftoc) S2M_FIELD_SET(CTakeDamageInfo, DamageInfoType, f, sp_ctof)
#define S2M_INFO_INT(f) S2M_FIELD_GET(CTakeDamageInfo, DamageInfoType, f, S2M_CELL) S2M_FIELD_SET(CTakeDamageInfo, DamageInfoType, f, S2M_CELL)
#define S2M_INFO_BOOL(f) S2M_FIELD_GET(CTakeDamageInfo, DamageInfoType, f, S2M_CELL) S2M_FIELD_SET(CTakeDamageInfo, DamageInfoType, f, S2M_BOOL)
#define S2M_INFO_HANDLE(f) S2M_FIELD_GET(CTakeDamageInfo, DamageInfoType, f, S2M_ENTITY) S2M_FIELD_SET(CTakeDamageInfo, DamageInfoType, f, S2M_HANDLE)
        S2M_INFO_FLOAT(m_flDamage)
        S2M_FIELD_GET(CTakeDamageInfo, DamageInfoType, m_flOriginalDamage, sp_ftoc)
        S2M_INFO_FLOAT(m_flTotalledDamage)
        S2M_INFO_FLOAT(m_flFriendlyFireDamageReductionRatio)
        S2M_INFO_INT(m_bitsDamageType)
        S2M_INFO_INT(m_iDamageCustom)
        S2M_INFO_INT(m_iAmmoType)
        S2M_INFO_INT(m_nNumObjectsPenetrated)
        S2M_INFO_BOOL(m_bShouldBleed)
        S2M_INFO_BOOL(m_bShouldSpark)
        S2M_INFO_BOOL(m_bStoppedBullet)
        S2M_FIELD_GET(CTakeDamageInfo, DamageInfoType, m_bInTakeDamageFlow, S2M_CELL)
        S2M_INFO_HANDLE(m_hInflictor)
        S2M_INFO_HANDLE(m_hAttacker)
        S2M_INFO_HANDLE(m_hAbility)
        S2M_FIELD_VEC(CTakeDamageInfo, DamageInfoType, m_vecDamageForce, 3)
        S2M_FIELD_VEC(CTakeDamageInfo, DamageInfoType, m_vecDamagePosition, 3)
        S2M_FIELD_VEC(CTakeDamageInfo, DamageInfoType, m_vecReportedPosition, 3)
        S2M_FIELD_VEC(CTakeDamageInfo, DamageInfoType, m_vecDamageDirection, 3)
#undef S2M_INFO_FLOAT
#undef S2M_INFO_INT
#undef S2M_INFO_BOOL
#undef S2M_INFO_HANDLE

        cell_t N_CTakeDamageInfo_get_m_iHitGroupId(IPluginContext* ctx, const cell_t* params)
        {
            CTakeDamageInfo* i = Lent<CTakeDamageInfo>(ctx, params[1], DamageInfoType());
            return i ? static_cast<cell_t>(i->m_iHitGroupId) : 0;
        }

        cell_t N_CTakeDamageInfo_set_m_iHitGroupId(IPluginContext* ctx, const cell_t* params)
        {
            if (CTakeDamageInfo* i = Lent<CTakeDamageInfo>(ctx, params[1], DamageInfoType()))
                i->m_iHitGroupId = static_cast<HitGroup_t>(params[2]);
            return 0;
        }

        cell_t N_CTakeDamageInfo_m_nDamageFlags(IPluginContext* ctx, const cell_t* params)
        {
            if (CTakeDamageInfo* i = Lent<CTakeDamageInfo>(ctx, params[1], DamageInfoType()))
                WriteMask64(ctx, params[2], static_cast<uint64_t>(i->m_nDamageFlags));
            return 0;
        }

        cell_t N_CTakeDamageInfo_Set_m_nDamageFlags(IPluginContext* ctx, const cell_t* params)
        {
            if (CTakeDamageInfo* i = Lent<CTakeDamageInfo>(ctx, params[1], DamageInfoType()))
                i->m_nDamageFlags = static_cast<TakeDamageFlags_t>(ReadMask64(ctx, params[2]));
            return 0;
        }

        cell_t N_CTakeDamageInfo_GetHitGroup(IPluginContext* ctx, const cell_t* params)
        {
            CTakeDamageInfo* i = Lent<CTakeDamageInfo>(ctx, params[1], DamageInfoType());
            return i ? static_cast<cell_t>(i->GetHitGroup()) : 0;
        }

        cell_t N_CTakeDamageInfo_GetTrace(IPluginContext* ctx, const cell_t* params)
        {
            CTakeDamageInfo* i = Lent<CTakeDamageInfo>(ctx, params[1], DamageInfoType());
            return i && i->m_pTrace ? NewTraceResult(ctx, i->m_pTrace) : 0;
        }

        // m_AttackerInfo (AttackerInfo_t): GetAttackerInfo(&isPawn, &isWorld, &slot, &team) -> the attacker pawn.
        cell_t N_CTakeDamageInfo_GetAttackerInfo(IPluginContext* ctx, const cell_t* params)
        {
            CTakeDamageInfo* i = Lent<CTakeDamageInfo>(ctx, params[1], DamageInfoType());
            if (!i)
                return -1;
            const AttackerInfo_t& a = i->m_AttackerInfo;
            cell_t* out;
            ctx->LocalToPhysAddr(params[2], &out);
            *out = a.m_bIsPawn;
            ctx->LocalToPhysAddr(params[3], &out);
            *out = a.m_bIsWorld;
            ctx->LocalToPhysAddr(params[4], &out);
            *out = a.m_nAttackerPlayerSlot;
            ctx->LocalToPhysAddr(params[5], &out);
            *out = a.m_nTeam;
            return EntityCell(a.m_hAttackerPawn.Get());
        }

        // CTakeDamageResult
#define S2M_RES(f, get, set) S2M_FIELD_GET(CTakeDamageResult, DamageResultType, f, get) S2M_FIELD_SET(CTakeDamageResult, DamageResultType, f, set)
        S2M_RES(m_nHealthLost, S2M_CELL, S2M_CELL)
        S2M_FIELD_GET(CTakeDamageResult, DamageResultType, m_nHealthBefore, S2M_CELL)
        S2M_RES(m_flDamageDealt, sp_ftoc, sp_ctof)
        S2M_FIELD_GET(CTakeDamageResult, DamageResultType, m_flPreModifiedDamage, sp_ftoc)
        S2M_RES(m_nTotalledHealthLost, S2M_CELL, S2M_CELL)
        S2M_RES(m_flTotalledDamageDealt, sp_ftoc, sp_ctof)
        S2M_FIELD_GET(CTakeDamageResult, DamageResultType, m_flTotalledPreModifiedDamage, sp_ftoc)
        S2M_FIELD_GET(CTakeDamageResult, DamageResultType, m_flNewDamageAccumulatorValue, sp_ftoc)
        S2M_RES(m_bWasDamageSuppressed, S2M_CELL, S2M_BOOL)
        S2M_RES(m_bSuppressFlinch, S2M_CELL, S2M_BOOL)
#undef S2M_RES

        cell_t N_CTakeDamageResult_get_m_nOverrideFlinchHitGroup(IPluginContext* ctx, const cell_t* params)
        {
            CTakeDamageResult* r = Lent<CTakeDamageResult>(ctx, params[1], DamageResultType());
            return r ? static_cast<cell_t>(r->m_nOverrideFlinchHitGroup) : 0;
        }

        cell_t N_CTakeDamageResult_set_m_nOverrideFlinchHitGroup(IPluginContext* ctx, const cell_t* params)
        {
            if (CTakeDamageResult* r = Lent<CTakeDamageResult>(ctx, params[1], DamageResultType()))
                r->m_nOverrideFlinchHitGroup = static_cast<HitGroup_t>(params[2]);
            return 0;
        }

        cell_t N_CTakeDamageResult_m_nDamageFlags(IPluginContext* ctx, const cell_t* params)
        {
            if (CTakeDamageResult* r = Lent<CTakeDamageResult>(ctx, params[1], DamageResultType()))
                WriteMask64(ctx, params[2], static_cast<uint64_t>(r->m_nDamageFlags));
            return 0;
        }

        cell_t N_CTakeDamageResult_GetOriginatingInfo(IPluginContext* ctx, const cell_t* params)
        {
            CTakeDamageResult* r = Lent<CTakeDamageResult>(ctx, params[1], DamageResultType());
            return r ? Lend(DamageInfoType(), r->m_pOriginatingInfo) : 0;
        }

        // CMoveData
#define S2M_MOVE(f, get, set) S2M_FIELD_GET(CMoveData, MoveDataType, f, get) S2M_FIELD_SET(CMoveData, MoveDataType, f, set)
        S2M_FIELD_GET(CMoveData, MoveDataType, m_nPlayerHandle, S2M_ENTITY)
        S2M_MOVE(m_flForwardMove, sp_ftoc, sp_ctof)
        S2M_MOVE(m_flSideMove, sp_ftoc, sp_ctof)
        S2M_MOVE(m_flUpMove, sp_ftoc, sp_ctof)
        S2M_MOVE(m_bHasSubtickInputs, S2M_CELL, S2M_BOOL)
        S2M_FIELD_GET(CMoveData, MoveDataType, m_nTickCount, S2M_CELL)
        S2M_FIELD_GET(CMoveData, MoveDataType, m_nTargetTick, S2M_CELL)
        S2M_MOVE(m_flSubtickStartFraction, sp_ftoc, sp_ctof)
        S2M_MOVE(m_flSubtickEndFraction, sp_ftoc, sp_ctof)
        S2M_MOVE(m_flMaxSpeed, sp_ftoc, sp_ctof)
        S2M_MOVE(m_flClientMaxSpeed, sp_ftoc, sp_ctof)
        S2M_MOVE(m_flFrictionDecel, sp_ftoc, sp_ctof)
        S2M_MOVE(m_bInAir, S2M_CELL, S2M_BOOL)
        S2M_MOVE(m_bGameCodeMovedPlayer, S2M_CELL, S2M_BOOL)
        S2M_FIELD_VEC(CMoveData, MoveDataType, m_vecAbsViewAngles, 3)
        S2M_FIELD_VEC(CMoveData, MoveDataType, m_vecViewAngles, 3)
        S2M_FIELD_VEC(CMoveData, MoveDataType, m_vecLastMovementImpulses, 3)
        S2M_FIELD_VEC(CMoveData, MoveDataType, m_vecVelocity, 3)
        S2M_FIELD_VEC(CMoveData, MoveDataType, m_vecAngles, 3)
        S2M_FIELD_VEC(CMoveData, MoveDataType, m_vecCollisionNormal, 3)
        S2M_FIELD_VEC(CMoveData, MoveDataType, m_vecGroundNormal, 3)
        S2M_FIELD_VEC(CMoveData, MoveDataType, m_vecAbsOrigin, 3)
        S2M_FIELD_VEC(CMoveData, MoveDataType, m_vecOutWishVel, 3)
        S2M_FIELD_VEC(CMoveData, MoveDataType, m_vecOldAngles, 3)
        S2M_FIELD_VEC(CMoveData, MoveDataType, m_vecAcceleration, 3)
        S2M_FIELD_VEC(CMoveData, MoveDataType, m_vecContinuousAcceleration, 3)
        S2M_FIELD_VEC(CMoveData, MoveDataType, m_vecWalkWishedVelocity, 2)
#undef S2M_MOVE

        cell_t N_CMoveData_HasZeroFrametime(IPluginContext* ctx, const cell_t* params)
        {
            CMoveData* m = Lent<CMoveData>(ctx, params[1], MoveDataType());
            return m && m->HasZeroFrametime();
        }

        cell_t N_CMoveData_IsLateCommand(IPluginContext* ctx, const cell_t* params)
        {
            CMoveData* m = Lent<CMoveData>(ctx, params[1], MoveDataType());
            return m && m->IsLateCommand();
        }

        cell_t N_CMoveData_SetHasZeroFrametime(IPluginContext* ctx, const cell_t* params)
        {
            if (CMoveData* m = Lent<CMoveData>(ctx, params[1], MoveDataType()))
                m->SetHasZeroFrametime(params[2] != 0);
            return 0;
        }

        cell_t N_CMoveData_SetIsLateCommand(IPluginContext* ctx, const cell_t* params)
        {
            if (CMoveData* m = Lent<CMoveData>(ctx, params[1], MoveDataType()))
                m->SetIsLateCommand(params[2] != 0);
            return 0;
        }

        cell_t N_CMoveData_GetTouchCount(IPluginContext* ctx, const cell_t* params)
        {
            CMoveData* m = Lent<CMoveData>(ctx, params[1], MoveDataType());
            return m ? m->m_TouchList.Count() : 0;
        }

        // GetTouch(index, float deltaVelocity[3]) -> a copy of the touch's trace
        cell_t N_CMoveData_GetTouch(IPluginContext* ctx, const cell_t* params)
        {
            CMoveData* m = Lent<CMoveData>(ctx, params[1], MoveDataType());
            if (!m)
                return 0;
            if (params[2] < 0 || params[2] >= m->m_TouchList.Count())
                return ctx->ThrowNativeError("Touch %d is out of range (%d touches)", params[2], m->m_TouchList.Count());
            const TouchList_t& touch = m->m_TouchList[params[2]];
            VecOut(ctx, params[3], &touch.m_vecDeltaVelocity.x);
            return NewTraceResult(ctx, &touch.m_Trace);
        }

        cell_t N_CMoveData_GetSubtickMoveCount(IPluginContext* ctx, const cell_t* params)
        {
            CMoveData* m = Lent<CMoveData>(ctx, params[1], MoveDataType());
            if (!m)
                return 0;
            return params[2] ? m->m_AttackSubtickMoves.Count() : m->m_SubtickMoves.Count();
        }

        // GetSubtickMove(index, button[2], &pressed, analog[4], attack) -> when
        cell_t N_CMoveData_GetSubtickMove(IPluginContext* ctx, const cell_t* params)
        {
            CMoveData* m = Lent<CMoveData>(ctx, params[1], MoveDataType());
            if (!m)
                return 0;
            const CUtlVector<SubtickMove>& moves = params[6] ? m->m_AttackSubtickMoves : m->m_SubtickMoves;
            const cell_t i = params[2];
            if (i < 0 || i >= moves.Count())
                return ctx->ThrowNativeError("Subtick move %d is out of range (%d moves)", i, moves.Count());
            const SubtickMove& move = moves[i];
            WriteMask64(ctx, params[3], move.m_nButton);
            cell_t* pressed;
            ctx->LocalToPhysAddr(params[4], &pressed);
            cell_t* analog;
            ctx->LocalToPhysAddr(params[5], &analog);
            if (move.IsAnalogInput())
            {
                *pressed = 0;
                analog[0] = sp_ftoc(move.m_AnalogMove.m_flAnalogForwardDelta);
                analog[1] = sp_ftoc(move.m_AnalogMove.m_flAnalogLeftDelta);
                analog[2] = sp_ftoc(move.m_AnalogMove.m_flPitchDelta);
                analog[3] = sp_ftoc(move.m_AnalogMove.m_flYawDelta);
            }
            else
            {
                *pressed = move.m_bPressed;
                for (int k = 0; k < 4; k++)
                    analog[k] = sp_ftoc(0.0f);
            }
            return sp_ftoc(move.m_flWhen);
        }
#undef S2M_FIELD_GET
#undef S2M_FIELD_SET
#undef S2M_FIELD_VEC
#undef S2M_CELL
#undef S2M_BOOL
#undef S2M_ENTITY
#undef S2M_HANDLE

        // ---- transmit -----------------------------------------------------------------

        transmit::TransmitManager& TM()
        {
            return transmit::transmitManager;
        }

        cell_t N_THookEntity(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* e = EntityAt(ctx, params[1]);
            return e && TM().HookEntity(CallerId(ctx), e, params[2] != 0);
        }

        cell_t N_TUnhookEntity(IPluginContext* ctx, const cell_t* params)
        {
            if (CEntityInstance* e = EntityAt(ctx, params[1]))
                TM().UnhookEntity(e);
            return 0;
        }

        cell_t N_TIsEntityHooked(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* e = EntityAt(ctx, params[1]);
            return e && TM().IsEntityHooked(e);
        }

        cell_t N_TSetVisible(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* e = EntityAt(ctx, params[1]);
            return e && TM().SetVisible(e, CPlayerSlot(params[2]), params[3] != 0, params[4]);
        }

        cell_t N_TSetVisibleToAll(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* e = EntityAt(ctx, params[1]);
            return e && TM().SetVisibleToAll(e, params[2] != 0, params[3]);
        }

        cell_t N_TIsVisible(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* e = EntityAt(ctx, params[1]);
            return e && TM().IsVisible(e, CPlayerSlot(params[2]));
        }

        cell_t N_TIsHiddenOnChannel(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* e = EntityAt(ctx, params[1]);
            return e && TM().IsHiddenOnChannel(e, CPlayerSlot(params[2]), params[3]);
        }

        cell_t N_TSetBlockAll(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* e = EntityAt(ctx, params[1]);
            return e && TM().SetBlockAll(e, params[2] != 0);
        }

        cell_t N_TGetBlockAll(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* e = EntityAt(ctx, params[1]);
            return e && TM().GetBlockAll(e);
        }

        cell_t N_TSetOwner(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* e = EntityAt(ctx, params[1]);
            CEntityInstance* owner = params[2] == -1 ? nullptr : EntityAt(ctx, params[2]);
            return e && (owner || params[2] == -1) && TM().SetOwner(e, owner);
        }

        cell_t N_TGetOwner(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* e = EntityAt(ctx, params[1]);
            return e ? EntityCell(TM().GetOwner(e)) : -1;
        }

        cell_t N_TResetViewer(IPluginContext*, const cell_t* params)
        {
            TM().ResetViewer(CPlayerSlot(params[1]));
            return 0;
        }

        HandleType InfoListType()
        {
            static const HandleType t = FrameType("ISource2ModTransmitInfoList");
            return t;
        }

        HandleType TransmitInfoType()
        {
            static const HandleType t = FrameType("ISource2ModTransmitInfo");
            return t;
        }

        struct InfoList
        {
            ISource2ModTransmitInfo* const* infos;
            int count;
            PluginId owner;
            std::vector<cell_t> handles;    // one per viewer, made on first Get()
        };

        cell_t N_THookCheckTransmit(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[1]);
            if (!fn)
                return 0;
            const PluginId owner = CallerId(ctx);
            CheckTransmitHook handler([fn, owner](ISource2ModTransmitInfo* const* infos, int infoCount, const uint16_t* indices, int entityCount)
            {
                InfoList list{ infos, infoCount, owner, std::vector<cell_t>(static_cast<size_t>(std::max(infoCount, 0)), 0) };
                const cell_t listHandle = Handles().Create(InfoListType(), &list, owner);

                std::vector<cell_t> cells(indices, indices + std::max(entityCount, 0));
                sp::CallArgs args;
                args.PushCell(listHandle);
                args.PushCell(infoCount);
                args.PushArray(cells.data(), static_cast<unsigned>(cells.size()));
                args.PushCell(entityCount);
                Invoke(fn, args);

                for (cell_t h : list.handles)
                    if (h)
                        Handles().Free(h, 0, true);
                Handles().Free(listHandle, 0, true);
            });
            return static_cast<cell_t>(TM().HookCheckTransmit(owner, std::move(handler)));
        }

        cell_t N_TUnhookCheckTransmit(IPluginContext*, const cell_t* params)
        {
            return static_cast<ISource2ModTransmit&>(TM()).UnhookCheckTransmit(static_cast<Source2ModTransmitHookId>(params[1]));
        }

        cell_t N_ListGet(IPluginContext* ctx, const cell_t* params)
        {
            auto* list = static_cast<InfoList*>(ReadHandle(ctx, params[1], InfoListType()));
            if (!list)
                return 0;
            const cell_t i = params[2];
            if (i < 0 || i >= list->count)
                return ctx->ThrowNativeError("Viewer %d is out of range (%d viewers)", i, list->count);
            if (!list->handles[i])
                list->handles[i] = Handles().Create(TransmitInfoType(), list->infos[i], list->owner);
            return list->handles[i];
        }

        ISource2ModTransmitInfo* InfoOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<ISource2ModTransmitInfo*>(ReadHandle(ctx, handle, TransmitInfoType()));
        }

        cell_t N_InfoGetPlayerSlot(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModTransmitInfo* i = InfoOf(ctx, params[1]);
            return i ? i->GetPlayerSlot().Get() : -1;
        }

        cell_t N_InfoIsFullUpdate(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModTransmitInfo* i = InfoOf(ctx, params[1]);
            return i && i->IsFullUpdate();
        }

        cell_t N_InfoIsTransmitting(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModTransmitInfo* i = InfoOf(ctx, params[1]);
            return i && i->IsTransmitting(params[2]);
        }

        cell_t N_InfoBlockTransmit(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModTransmitInfo* i = InfoOf(ctx, params[1]))
                i->BlockTransmit(params[2]);
            return 0;
        }

        cell_t N_InfoAllowTransmit(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModTransmitInfo* i = InfoOf(ctx, params[1]))
                i->AllowTransmit(params[2]);
            return 0;
        }

        // Get/SetTransmitEntity, ...NonPlayers, ...OutOfPVS, ...Always: one bit of a set.
        template <CBitVec<MAX_EDICTS>* (ISource2ModTransmitInfo::*SET)()>
        CBitVec<MAX_EDICTS>* BitsOf(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModTransmitInfo* i = InfoOf(ctx, params[1]);
            if (!i)
                return nullptr;
            if (params[2] < 0 || params[2] >= MAX_EDICTS)
            {
                ctx->ThrowNativeError("Entity index %d is out of range", params[2]);
                return nullptr;
            }
            return (i->*SET)();
        }

        template <CBitVec<MAX_EDICTS>* (ISource2ModTransmitInfo::*SET)()>
        cell_t N_InfoGetBit(IPluginContext* ctx, const cell_t* params)
        {
            CBitVec<MAX_EDICTS>* bits = BitsOf<SET>(ctx, params);
            return bits && bits->IsBitSet(params[2]);
        }

        template <CBitVec<MAX_EDICTS>* (ISource2ModTransmitInfo::*SET)()>
        cell_t N_InfoSetBit(IPluginContext* ctx, const cell_t* params)
        {
            if (CBitVec<MAX_EDICTS>* bits = BitsOf<SET>(ctx, params))
            {
                if (params[3])
                    bits->Set(params[2]);
                else
                    bits->Clear(params[2]);
            }
            return 0;
        }

        const sp_nativeinfo_t g_gameHookNatives[] = {
            // source2mod/gamehooks.inc
            { "Source2ModGameHooks.HookTakeDamage", N_Hook<TakeDamageContext, &ISource2ModGameHooks::HookTakeDamage, &ISource2ModGameHooks::UnhookTakeDamage> },
            { "Source2ModGameHooks.UnhookTakeDamage", N_UnhookGameHook },
            { "Source2ModGameHooks.HookCanAcquire", N_Hook<CanAcquireContext, &ISource2ModGameHooks::HookCanAcquire, &ISource2ModGameHooks::UnhookCanAcquire> },
            { "Source2ModGameHooks.UnhookCanAcquire", N_UnhookGameHook },
            { "Source2ModGameHooks.HookCanMove", N_Hook<CanMoveContext, &ISource2ModGameHooks::HookCanMove, &ISource2ModGameHooks::UnhookCanMove> },
            { "Source2ModGameHooks.UnhookCanMove", N_UnhookGameHook },
            { "Source2ModGameHooks.HookCanUse", N_Hook<CanUseContext, &ISource2ModGameHooks::HookCanUse, &ISource2ModGameHooks::UnhookCanUse> },
            { "Source2ModGameHooks.UnhookCanUse", N_UnhookGameHook },
            { "Source2ModGameHooks.HookPostThink", N_Hook<PostThinkContext, &ISource2ModGameHooks::HookPostThink, &ISource2ModGameHooks::UnhookPostThink> },
            { "Source2ModGameHooks.UnhookPostThink", N_UnhookGameHook },
            { "Source2ModGameHooks.HookProcessUsercmds", N_Hook<ProcessUsercmdsContext, &ISource2ModGameHooks::HookProcessUsercmds, &ISource2ModGameHooks::UnhookProcessUsercmds> },
            { "Source2ModGameHooks.UnhookProcessUsercmds", N_UnhookGameHook },
            { "Source2ModGameHooks.HookSimulateUserCommands", N_Hook<SimulateUserCommandsContext, &ISource2ModGameHooks::HookSimulateUserCommands, &ISource2ModGameHooks::UnhookSimulateUserCommands> },
            { "Source2ModGameHooks.UnhookSimulateUserCommands", N_UnhookGameHook },
            { "Source2ModGameHooks.HookRunCommand", N_Hook<RunCommandContext, &ISource2ModGameHooks::HookRunCommand, &ISource2ModGameHooks::UnhookRunCommand> },
            { "Source2ModGameHooks.UnhookRunCommand", N_UnhookGameHook },
            { "Source2ModGameHooks.HookAcceptInput", N_Hook<AcceptInputContext, &ISource2ModGameHooks::HookAcceptInput, &ISource2ModGameHooks::UnhookAcceptInput> },
            { "Source2ModGameHooks.UnhookAcceptInput", N_UnhookGameHook },
            { "Source2ModGameHooks.HookTouch", N_Hook<TouchContext, &ISource2ModGameHooks::HookTouch, &ISource2ModGameHooks::UnhookTouch> },
            { "Source2ModGameHooks.UnhookTouch", N_UnhookGameHook },
            { "Source2ModGameHooks.HookDropWeapon", N_Hook<DropWeaponContext, &ISource2ModGameHooks::HookDropWeapon, &ISource2ModGameHooks::UnhookDropWeapon> },
            { "Source2ModGameHooks.UnhookDropWeapon", N_UnhookGameHook },
            { "Source2ModGameHooks.HookAirAccelerate", N_Hook<AirAccelerateContext, &ISource2ModGameHooks::HookAirAccelerate, &ISource2ModGameHooks::UnhookAirAccelerate> },
            { "Source2ModGameHooks.UnhookAirAccelerate", N_UnhookGameHook },
            { "Source2ModGameHooks.HookAirMove", N_Hook<MovementContext, &ISource2ModGameHooks::HookAirMove, &ISource2ModGameHooks::UnhookAirMove> },
            { "Source2ModGameHooks.UnhookAirMove", N_UnhookGameHook },
            { "Source2ModGameHooks.HookCanUnduck", N_Hook<MovementContext, &ISource2ModGameHooks::HookCanUnduck, &ISource2ModGameHooks::UnhookCanUnduck> },
            { "Source2ModGameHooks.UnhookCanUnduck", N_UnhookGameHook },
            { "Source2ModGameHooks.HookCategorizePosition", N_Hook<CategorizePositionContext, &ISource2ModGameHooks::HookCategorizePosition, &ISource2ModGameHooks::UnhookCategorizePosition> },
            { "Source2ModGameHooks.UnhookCategorizePosition", N_UnhookGameHook },
            { "Source2ModGameHooks.HookCheckFalling", N_Hook<MovementContext, &ISource2ModGameHooks::HookCheckFalling, &ISource2ModGameHooks::UnhookCheckFalling> },
            { "Source2ModGameHooks.UnhookCheckFalling", N_UnhookGameHook },
            { "Source2ModGameHooks.HookCheckParameters", N_Hook<MovementContext, &ISource2ModGameHooks::HookCheckParameters, &ISource2ModGameHooks::UnhookCheckParameters> },
            { "Source2ModGameHooks.UnhookCheckParameters", N_UnhookGameHook },
            { "Source2ModGameHooks.HookCheckVelocity", N_Hook<CheckVelocityContext, &ISource2ModGameHooks::HookCheckVelocity, &ISource2ModGameHooks::UnhookCheckVelocity> },
            { "Source2ModGameHooks.UnhookCheckVelocity", N_UnhookGameHook },
            { "Source2ModGameHooks.HookCheckWater", N_Hook<MovementContext, &ISource2ModGameHooks::HookCheckWater, &ISource2ModGameHooks::UnhookCheckWater> },
            { "Source2ModGameHooks.UnhookCheckWater", N_UnhookGameHook },
            { "Source2ModGameHooks.HookDuck", N_Hook<MovementContext, &ISource2ModGameHooks::HookDuck, &ISource2ModGameHooks::UnhookDuck> },
            { "Source2ModGameHooks.UnhookDuck", N_UnhookGameHook },
            { "Source2ModGameHooks.HookFriction", N_Hook<MovementContext, &ISource2ModGameHooks::HookFriction, &ISource2ModGameHooks::UnhookFriction> },
            { "Source2ModGameHooks.UnhookFriction", N_UnhookGameHook },
            { "Source2ModGameHooks.HookFullWalkMove", N_Hook<FullWalkMoveContext, &ISource2ModGameHooks::HookFullWalkMove, &ISource2ModGameHooks::UnhookFullWalkMove> },
            { "Source2ModGameHooks.UnhookFullWalkMove", N_UnhookGameHook },
            { "Source2ModGameHooks.HookGroundAccelerate", N_Hook<GroundAccelerateContext, &ISource2ModGameHooks::HookGroundAccelerate, &ISource2ModGameHooks::UnhookGroundAccelerate> },
            { "Source2ModGameHooks.UnhookGroundAccelerate", N_UnhookGameHook },
            { "Source2ModGameHooks.HookLadderMove", N_Hook<MovementContext, &ISource2ModGameHooks::HookLadderMove, &ISource2ModGameHooks::UnhookLadderMove> },
            { "Source2ModGameHooks.UnhookLadderMove", N_UnhookGameHook },
            { "Source2ModGameHooks.HookMoveInit", N_Hook<MovementContext, &ISource2ModGameHooks::HookMoveInit, &ISource2ModGameHooks::UnhookMoveInit> },
            { "Source2ModGameHooks.UnhookMoveInit", N_UnhookGameHook },
            { "Source2ModGameHooks.HookPlayerMove", N_Hook<MovementContext, &ISource2ModGameHooks::HookPlayerMove, &ISource2ModGameHooks::UnhookPlayerMove> },
            { "Source2ModGameHooks.UnhookPlayerMove", N_UnhookGameHook },
            { "Source2ModGameHooks.HookProcessMovement", N_Hook<MovementContext, &ISource2ModGameHooks::HookProcessMovement, &ISource2ModGameHooks::UnhookProcessMovement> },
            { "Source2ModGameHooks.UnhookProcessMovement", N_UnhookGameHook },
            { "Source2ModGameHooks.HookSetupMove", N_Hook<SetupMoveContext, &ISource2ModGameHooks::HookSetupMove, &ISource2ModGameHooks::UnhookSetupMove> },
            { "Source2ModGameHooks.UnhookSetupMove", N_UnhookGameHook },
            { "Source2ModGameHooks.HookTryPlayerMove", N_Hook<TryPlayerMoveContext, &ISource2ModGameHooks::HookTryPlayerMove, &ISource2ModGameHooks::UnhookTryPlayerMove> },
            { "Source2ModGameHooks.UnhookTryPlayerMove", N_UnhookGameHook },
            { "Source2ModGameHooks.HookWalkMove", N_Hook<MovementContext, &ISource2ModGameHooks::HookWalkMove, &ISource2ModGameHooks::UnhookWalkMove> },
            { "Source2ModGameHooks.UnhookWalkMove", N_UnhookGameHook },
            { "Source2ModGameHooks.HookWaterMove", N_Hook<MovementContext, &ISource2ModGameHooks::HookWaterMove, &ISource2ModGameHooks::UnhookWaterMove> },
            { "Source2ModGameHooks.UnhookWaterMove", N_UnhookGameHook },
            { "Source2ModGameHooks.HookOnJumpLegacy", N_Hook<LegacyJumpContext, &ISource2ModGameHooks::HookOnJumpLegacy, &ISource2ModGameHooks::UnhookOnJumpLegacy> },
            { "Source2ModGameHooks.UnhookOnJumpLegacy", N_UnhookGameHook },
            { "Source2ModGameHooks.HookOnJumpModern", N_Hook<ModernJumpContext, &ISource2ModGameHooks::HookOnJumpModern, &ISource2ModGameHooks::UnhookOnJumpModern> },
            { "Source2ModGameHooks.UnhookOnJumpModern", N_UnhookGameHook },
            { "Source2ModGameHooks.HookCheckJumpButtonLegacy", N_Hook<LegacyJumpContext, &ISource2ModGameHooks::HookCheckJumpButtonLegacy, &ISource2ModGameHooks::UnhookCheckJumpButtonLegacy> },
            { "Source2ModGameHooks.UnhookCheckJumpButtonLegacy", N_UnhookGameHook },
            { "Source2ModGameHooks.HookCheckJumpButtonModern", N_Hook<ModernJumpContext, &ISource2ModGameHooks::HookCheckJumpButtonModern, &ISource2ModGameHooks::UnhookCheckJumpButtonModern> },
            { "Source2ModGameHooks.UnhookCheckJumpButtonModern", N_UnhookGameHook },
            { "Source2ModGameHooks.IsAvailable", N_IsAvailable },
            { "TakeDamageContext.entity.get", N_TakeDamageContext_entity },
            { "TakeDamageContext.info.get", N_TakeDamageContext_info },
            { "TakeDamageContext.damageResult.get", N_TakeDamageContext_damageResult },
            { "TakeDamageContext.CallOriginal", N_CallOriginal<TakeDamageContext> },
            { "CanAcquireContext.services.get", N_CanAcquireContext_services },
            { "CanAcquireContext.pawn.get", N_CanAcquireContext_pawn },
            { "CanAcquireContext.item.get", N_CanAcquireContext_item },
            { "CanAcquireContext.method.get", N_CanAcquireContext_method },
            { "CanAcquireContext.CallOriginal", N_CallOriginal<CanAcquireContext> },
            { "CanMoveContext.pawn.get", N_CanMoveContext_pawn },
            { "CanMoveContext.CallOriginal", N_CallOriginal<CanMoveContext> },
            { "CanUseContext.services.get", N_CanUseContext_services },
            { "CanUseContext.pawn.get", N_CanUseContext_pawn },
            { "CanUseContext.weapon.get", N_CanUseContext_weapon },
            { "CanUseContext.CallOriginal", N_CallOriginal<CanUseContext> },
            { "PostThinkContext.pawn.get", N_PostThinkContext_pawn },
            { "PostThinkContext.CallOriginal", N_CallOriginal<PostThinkContext> },
            { "ProcessUsercmdsContext.controller.get", N_ProcessUsercmdsContext_controller },
            { "ProcessUsercmdsContext.cmds.get", N_ProcessUsercmdsContext_cmds },
            { "ProcessUsercmdsContext.count.get", N_ProcessUsercmdsContext_count },
            { "ProcessUsercmdsContext.paused.get", N_ProcessUsercmdsContext_paused },
            { "ProcessUsercmdsContext.margin.get", N_ProcessUsercmdsContext_margin },
            { "ProcessUsercmdsContext.CallOriginal", N_CallOriginal<ProcessUsercmdsContext> },
            { "SimulateUserCommandsContext.controller.get", N_SimulateUserCommandsContext_controller },
            { "SimulateUserCommandsContext.CallOriginal", N_CallOriginal<SimulateUserCommandsContext> },
            { "RunCommandContext.services.get", N_RunCommandContext_services },
            { "RunCommandContext.pawn.get", N_RunCommandContext_pawn },
            { "RunCommandContext.cmd.get", N_RunCommandContext_cmd },
            { "RunCommandContext.CallOriginal", N_CallOriginal<RunCommandContext> },
            { "AcceptInputContext.entity.get", N_AcceptInputContext_entity },
            { "AcceptInputContext.activator.get", N_AcceptInputContext_activator },
            { "AcceptInputContext.caller.get", N_AcceptInputContext_caller },
            { "AcceptInputContext.GetInputName", N_AcceptInputContext_GetInputName },
            { "AcceptInputContext.GetValue", N_AcceptInputContext_GetValue },
            { "AcceptInputContext.SetValue", N_AcceptInputContext_SetValue },
            { "AcceptInputContext.CallOriginal", N_CallOriginal<AcceptInputContext> },
            { "TouchContext.entity.get", N_TouchContext_entity },
            { "TouchContext.other.get", N_TouchContext_other },
            { "TouchContext.CallOriginal", N_CallOriginal<TouchContext> },
            { "DropWeaponContext.services.get", N_DropWeaponContext_services },
            { "DropWeaponContext.pawn.get", N_DropWeaponContext_pawn },
            { "DropWeaponContext.weapon.get", N_DropWeaponContext_weapon },
            { "DropWeaponContext.GetTarget", N_DropWeaponContext_GetTarget },
            { "DropWeaponContext.SetTarget", N_DropWeaponContext_SetTarget },
            { "DropWeaponContext.GetVelocity", N_DropWeaponContext_GetVelocity },
            { "DropWeaponContext.SetVelocity", N_DropWeaponContext_SetVelocity },
            { "DropWeaponContext.CallOriginal", N_CallOriginal<DropWeaponContext> },
            { "MovementContext.services.get", N_MovementContext_services },
            { "MovementContext.pawn.get", N_MovementContext_pawn },
            { "MovementContext.moveData.get", N_MovementContext_moveData },
            { "MovementContext.CallOriginal", N_CallOriginal<MovementContext> },
            { "AirAccelerateContext.services.get", N_AirAccelerateContext_services },
            { "AirAccelerateContext.pawn.get", N_AirAccelerateContext_pawn },
            { "AirAccelerateContext.moveData.get", N_AirAccelerateContext_moveData },
            { "AirAccelerateContext.wishSpeed.get", N_AirAccelerateContext_wishSpeed },
            { "AirAccelerateContext.acceleration.get", N_AirAccelerateContext_acceleration },
            { "AirAccelerateContext.GetWishDirection", N_AirAccelerateContext_GetWishDirection },
            { "AirAccelerateContext.SetWishDirection", N_AirAccelerateContext_SetWishDirection },
            { "AirAccelerateContext.CallOriginal", N_CallOriginal<AirAccelerateContext> },
            { "GroundAccelerateContext.services.get", N_GroundAccelerateContext_services },
            { "GroundAccelerateContext.pawn.get", N_GroundAccelerateContext_pawn },
            { "GroundAccelerateContext.moveData.get", N_GroundAccelerateContext_moveData },
            { "GroundAccelerateContext.frameTime.get", N_GroundAccelerateContext_frameTime },
            { "GroundAccelerateContext.wishSpeed.get", N_GroundAccelerateContext_wishSpeed },
            { "GroundAccelerateContext.acceleration.get", N_GroundAccelerateContext_acceleration },
            { "GroundAccelerateContext.GetWishDirection", N_GroundAccelerateContext_GetWishDirection },
            { "GroundAccelerateContext.SetWishDirection", N_GroundAccelerateContext_SetWishDirection },
            { "GroundAccelerateContext.CallOriginal", N_CallOriginal<GroundAccelerateContext> },
            { "CategorizePositionContext.services.get", N_CategorizePositionContext_services },
            { "CategorizePositionContext.pawn.get", N_CategorizePositionContext_pawn },
            { "CategorizePositionContext.moveData.get", N_CategorizePositionContext_moveData },
            { "CategorizePositionContext.stayOnGround.get", N_CategorizePositionContext_stayOnGround },
            { "CategorizePositionContext.CallOriginal", N_CallOriginal<CategorizePositionContext> },
            { "CheckVelocityContext.services.get", N_CheckVelocityContext_services },
            { "CheckVelocityContext.pawn.get", N_CheckVelocityContext_pawn },
            { "CheckVelocityContext.moveData.get", N_CheckVelocityContext_moveData },
            { "CheckVelocityContext.CallOriginal", N_CallOriginal<CheckVelocityContext> },
            { "FullWalkMoveContext.services.get", N_FullWalkMoveContext_services },
            { "FullWalkMoveContext.pawn.get", N_FullWalkMoveContext_pawn },
            { "FullWalkMoveContext.moveData.get", N_FullWalkMoveContext_moveData },
            { "FullWalkMoveContext.onGround.get", N_FullWalkMoveContext_onGround },
            { "FullWalkMoveContext.CallOriginal", N_CallOriginal<FullWalkMoveContext> },
            { "SetupMoveContext.services.get", N_SetupMoveContext_services },
            { "SetupMoveContext.pawn.get", N_SetupMoveContext_pawn },
            { "SetupMoveContext.moveData.get", N_SetupMoveContext_moveData },
            { "SetupMoveContext.cmd.get", N_SetupMoveContext_cmd },
            { "SetupMoveContext.CallOriginal", N_CallOriginal<SetupMoveContext> },
            { "TryPlayerMoveContext.services.get", N_TryPlayerMoveContext_services },
            { "TryPlayerMoveContext.pawn.get", N_TryPlayerMoveContext_pawn },
            { "TryPlayerMoveContext.moveData.get", N_TryPlayerMoveContext_moveData },
            { "TryPlayerMoveContext.GetFirstDest", N_TryPlayerMoveContext_GetFirstDest },
            { "TryPlayerMoveContext.SetFirstDest", N_TryPlayerMoveContext_SetFirstDest },
            { "TryPlayerMoveContext.GetFirstTrace", N_TryPlayerMoveContext_GetFirstTrace },
            { "TryPlayerMoveContext.GetIsSurfing", N_TryPlayerMoveContext_GetIsSurfing },
            { "TryPlayerMoveContext.SetIsSurfing", N_TryPlayerMoveContext_SetIsSurfing },
            { "TryPlayerMoveContext.CallOriginal", N_CallOriginal<TryPlayerMoveContext> },
            { "LegacyJumpContext.jump.get", N_LegacyJumpContext_jump },
            { "LegacyJumpContext.moveData.get", N_LegacyJumpContext_moveData },
            { "LegacyJumpContext.CallOriginal", N_CallOriginal<LegacyJumpContext> },
            { "ModernJumpContext.jump.get", N_ModernJumpContext_jump },
            { "ModernJumpContext.moveData.get", N_ModernJumpContext_moveData },
            { "ModernJumpContext.CallOriginal", N_CallOriginal<ModernJumpContext> },
            { "CTakeDamageInfo.m_flDamage.get", N_CTakeDamageInfo_get_m_flDamage },
            { "CTakeDamageInfo.m_flDamage.set", N_CTakeDamageInfo_set_m_flDamage },
            { "CTakeDamageInfo.m_flTotalledDamage.get", N_CTakeDamageInfo_get_m_flTotalledDamage },
            { "CTakeDamageInfo.m_flTotalledDamage.set", N_CTakeDamageInfo_set_m_flTotalledDamage },
            { "CTakeDamageInfo.m_bitsDamageType.get", N_CTakeDamageInfo_get_m_bitsDamageType },
            { "CTakeDamageInfo.m_bitsDamageType.set", N_CTakeDamageInfo_set_m_bitsDamageType },
            { "CTakeDamageInfo.m_iDamageCustom.get", N_CTakeDamageInfo_get_m_iDamageCustom },
            { "CTakeDamageInfo.m_iDamageCustom.set", N_CTakeDamageInfo_set_m_iDamageCustom },
            { "CTakeDamageInfo.m_iAmmoType.get", N_CTakeDamageInfo_get_m_iAmmoType },
            { "CTakeDamageInfo.m_iAmmoType.set", N_CTakeDamageInfo_set_m_iAmmoType },
            { "CTakeDamageInfo.m_iHitGroupId.get", N_CTakeDamageInfo_get_m_iHitGroupId },
            { "CTakeDamageInfo.m_iHitGroupId.set", N_CTakeDamageInfo_set_m_iHitGroupId },
            { "CTakeDamageInfo.m_bShouldBleed.get", N_CTakeDamageInfo_get_m_bShouldBleed },
            { "CTakeDamageInfo.m_bShouldBleed.set", N_CTakeDamageInfo_set_m_bShouldBleed },
            { "CTakeDamageInfo.m_bShouldSpark.get", N_CTakeDamageInfo_get_m_bShouldSpark },
            { "CTakeDamageInfo.m_bShouldSpark.set", N_CTakeDamageInfo_set_m_bShouldSpark },
            { "CTakeDamageInfo.m_bStoppedBullet.get", N_CTakeDamageInfo_get_m_bStoppedBullet },
            { "CTakeDamageInfo.m_bStoppedBullet.set", N_CTakeDamageInfo_set_m_bStoppedBullet },
            { "CTakeDamageInfo.m_nNumObjectsPenetrated.get", N_CTakeDamageInfo_get_m_nNumObjectsPenetrated },
            { "CTakeDamageInfo.m_nNumObjectsPenetrated.set", N_CTakeDamageInfo_set_m_nNumObjectsPenetrated },
            { "CTakeDamageInfo.m_flFriendlyFireDamageReductionRatio.get", N_CTakeDamageInfo_get_m_flFriendlyFireDamageReductionRatio },
            { "CTakeDamageInfo.m_flFriendlyFireDamageReductionRatio.set", N_CTakeDamageInfo_set_m_flFriendlyFireDamageReductionRatio },
            { "CTakeDamageInfo.m_hInflictor.get", N_CTakeDamageInfo_get_m_hInflictor },
            { "CTakeDamageInfo.m_hInflictor.set", N_CTakeDamageInfo_set_m_hInflictor },
            { "CTakeDamageInfo.m_hAttacker.get", N_CTakeDamageInfo_get_m_hAttacker },
            { "CTakeDamageInfo.m_hAttacker.set", N_CTakeDamageInfo_set_m_hAttacker },
            { "CTakeDamageInfo.m_hAbility.get", N_CTakeDamageInfo_get_m_hAbility },
            { "CTakeDamageInfo.m_hAbility.set", N_CTakeDamageInfo_set_m_hAbility },
            { "CTakeDamageInfo.m_flOriginalDamage.get", N_CTakeDamageInfo_get_m_flOriginalDamage },
            { "CTakeDamageInfo.m_bInTakeDamageFlow.get", N_CTakeDamageInfo_get_m_bInTakeDamageFlow },
            { "CTakeDamageInfo.m_nDamageFlags", N_CTakeDamageInfo_m_nDamageFlags },
            { "CTakeDamageInfo.Set_m_nDamageFlags", N_CTakeDamageInfo_Set_m_nDamageFlags },
            { "CTakeDamageInfo.m_vecDamageForce", N_CTakeDamageInfo_m_vecDamageForce },
            { "CTakeDamageInfo.Set_m_vecDamageForce", N_CTakeDamageInfo_Set_m_vecDamageForce },
            { "CTakeDamageInfo.m_vecDamagePosition", N_CTakeDamageInfo_m_vecDamagePosition },
            { "CTakeDamageInfo.Set_m_vecDamagePosition", N_CTakeDamageInfo_Set_m_vecDamagePosition },
            { "CTakeDamageInfo.m_vecReportedPosition", N_CTakeDamageInfo_m_vecReportedPosition },
            { "CTakeDamageInfo.Set_m_vecReportedPosition", N_CTakeDamageInfo_Set_m_vecReportedPosition },
            { "CTakeDamageInfo.m_vecDamageDirection", N_CTakeDamageInfo_m_vecDamageDirection },
            { "CTakeDamageInfo.Set_m_vecDamageDirection", N_CTakeDamageInfo_Set_m_vecDamageDirection },
            { "CTakeDamageInfo.GetHitGroup", N_CTakeDamageInfo_GetHitGroup },
            { "CTakeDamageInfo.GetTrace", N_CTakeDamageInfo_GetTrace },
            { "CTakeDamageInfo.GetAttackerInfo", N_CTakeDamageInfo_GetAttackerInfo },
            { "CMoveData.GetTouchCount", N_CMoveData_GetTouchCount },
            { "CMoveData.GetTouch", N_CMoveData_GetTouch },
            { "CTakeDamageResult.m_nHealthLost.get", N_CTakeDamageResult_get_m_nHealthLost },
            { "CTakeDamageResult.m_nHealthLost.set", N_CTakeDamageResult_set_m_nHealthLost },
            { "CTakeDamageResult.m_flDamageDealt.get", N_CTakeDamageResult_get_m_flDamageDealt },
            { "CTakeDamageResult.m_flDamageDealt.set", N_CTakeDamageResult_set_m_flDamageDealt },
            { "CTakeDamageResult.m_nTotalledHealthLost.get", N_CTakeDamageResult_get_m_nTotalledHealthLost },
            { "CTakeDamageResult.m_nTotalledHealthLost.set", N_CTakeDamageResult_set_m_nTotalledHealthLost },
            { "CTakeDamageResult.m_flTotalledDamageDealt.get", N_CTakeDamageResult_get_m_flTotalledDamageDealt },
            { "CTakeDamageResult.m_flTotalledDamageDealt.set", N_CTakeDamageResult_set_m_flTotalledDamageDealt },
            { "CTakeDamageResult.m_bWasDamageSuppressed.get", N_CTakeDamageResult_get_m_bWasDamageSuppressed },
            { "CTakeDamageResult.m_bWasDamageSuppressed.set", N_CTakeDamageResult_set_m_bWasDamageSuppressed },
            { "CTakeDamageResult.m_bSuppressFlinch.get", N_CTakeDamageResult_get_m_bSuppressFlinch },
            { "CTakeDamageResult.m_bSuppressFlinch.set", N_CTakeDamageResult_set_m_bSuppressFlinch },
            { "CTakeDamageResult.m_nOverrideFlinchHitGroup.get", N_CTakeDamageResult_get_m_nOverrideFlinchHitGroup },
            { "CTakeDamageResult.m_nOverrideFlinchHitGroup.set", N_CTakeDamageResult_set_m_nOverrideFlinchHitGroup },
            { "CTakeDamageResult.m_nHealthBefore.get", N_CTakeDamageResult_get_m_nHealthBefore },
            { "CTakeDamageResult.m_flPreModifiedDamage.get", N_CTakeDamageResult_get_m_flPreModifiedDamage },
            { "CTakeDamageResult.m_flTotalledPreModifiedDamage.get", N_CTakeDamageResult_get_m_flTotalledPreModifiedDamage },
            { "CTakeDamageResult.m_flNewDamageAccumulatorValue.get", N_CTakeDamageResult_get_m_flNewDamageAccumulatorValue },
            { "CTakeDamageResult.m_nDamageFlags", N_CTakeDamageResult_m_nDamageFlags },
            { "CTakeDamageResult.GetOriginatingInfo", N_CTakeDamageResult_GetOriginatingInfo },
            { "CMoveData.m_flForwardMove.get", N_CMoveData_get_m_flForwardMove },
            { "CMoveData.m_flForwardMove.set", N_CMoveData_set_m_flForwardMove },
            { "CMoveData.m_flSideMove.get", N_CMoveData_get_m_flSideMove },
            { "CMoveData.m_flSideMove.set", N_CMoveData_set_m_flSideMove },
            { "CMoveData.m_flUpMove.get", N_CMoveData_get_m_flUpMove },
            { "CMoveData.m_flUpMove.set", N_CMoveData_set_m_flUpMove },
            { "CMoveData.m_bHasSubtickInputs.get", N_CMoveData_get_m_bHasSubtickInputs },
            { "CMoveData.m_bHasSubtickInputs.set", N_CMoveData_set_m_bHasSubtickInputs },
            { "CMoveData.m_flSubtickStartFraction.get", N_CMoveData_get_m_flSubtickStartFraction },
            { "CMoveData.m_flSubtickStartFraction.set", N_CMoveData_set_m_flSubtickStartFraction },
            { "CMoveData.m_flSubtickEndFraction.get", N_CMoveData_get_m_flSubtickEndFraction },
            { "CMoveData.m_flSubtickEndFraction.set", N_CMoveData_set_m_flSubtickEndFraction },
            { "CMoveData.m_flMaxSpeed.get", N_CMoveData_get_m_flMaxSpeed },
            { "CMoveData.m_flMaxSpeed.set", N_CMoveData_set_m_flMaxSpeed },
            { "CMoveData.m_flClientMaxSpeed.get", N_CMoveData_get_m_flClientMaxSpeed },
            { "CMoveData.m_flClientMaxSpeed.set", N_CMoveData_set_m_flClientMaxSpeed },
            { "CMoveData.m_flFrictionDecel.get", N_CMoveData_get_m_flFrictionDecel },
            { "CMoveData.m_flFrictionDecel.set", N_CMoveData_set_m_flFrictionDecel },
            { "CMoveData.m_bInAir.get", N_CMoveData_get_m_bInAir },
            { "CMoveData.m_bInAir.set", N_CMoveData_set_m_bInAir },
            { "CMoveData.m_bGameCodeMovedPlayer.get", N_CMoveData_get_m_bGameCodeMovedPlayer },
            { "CMoveData.m_bGameCodeMovedPlayer.set", N_CMoveData_set_m_bGameCodeMovedPlayer },
            { "CMoveData.m_nPlayerHandle.get", N_CMoveData_get_m_nPlayerHandle },
            { "CMoveData.m_nTickCount.get", N_CMoveData_get_m_nTickCount },
            { "CMoveData.m_nTargetTick.get", N_CMoveData_get_m_nTargetTick },
            { "CMoveData.m_vecAbsViewAngles", N_CMoveData_m_vecAbsViewAngles },
            { "CMoveData.Set_m_vecAbsViewAngles", N_CMoveData_Set_m_vecAbsViewAngles },
            { "CMoveData.m_vecViewAngles", N_CMoveData_m_vecViewAngles },
            { "CMoveData.Set_m_vecViewAngles", N_CMoveData_Set_m_vecViewAngles },
            { "CMoveData.m_vecLastMovementImpulses", N_CMoveData_m_vecLastMovementImpulses },
            { "CMoveData.Set_m_vecLastMovementImpulses", N_CMoveData_Set_m_vecLastMovementImpulses },
            { "CMoveData.m_vecVelocity", N_CMoveData_m_vecVelocity },
            { "CMoveData.Set_m_vecVelocity", N_CMoveData_Set_m_vecVelocity },
            { "CMoveData.m_vecAngles", N_CMoveData_m_vecAngles },
            { "CMoveData.Set_m_vecAngles", N_CMoveData_Set_m_vecAngles },
            { "CMoveData.m_vecCollisionNormal", N_CMoveData_m_vecCollisionNormal },
            { "CMoveData.Set_m_vecCollisionNormal", N_CMoveData_Set_m_vecCollisionNormal },
            { "CMoveData.m_vecGroundNormal", N_CMoveData_m_vecGroundNormal },
            { "CMoveData.Set_m_vecGroundNormal", N_CMoveData_Set_m_vecGroundNormal },
            { "CMoveData.m_vecAbsOrigin", N_CMoveData_m_vecAbsOrigin },
            { "CMoveData.Set_m_vecAbsOrigin", N_CMoveData_Set_m_vecAbsOrigin },
            { "CMoveData.m_vecOutWishVel", N_CMoveData_m_vecOutWishVel },
            { "CMoveData.Set_m_vecOutWishVel", N_CMoveData_Set_m_vecOutWishVel },
            { "CMoveData.m_vecOldAngles", N_CMoveData_m_vecOldAngles },
            { "CMoveData.Set_m_vecOldAngles", N_CMoveData_Set_m_vecOldAngles },
            { "CMoveData.m_vecAcceleration", N_CMoveData_m_vecAcceleration },
            { "CMoveData.Set_m_vecAcceleration", N_CMoveData_Set_m_vecAcceleration },
            { "CMoveData.m_vecContinuousAcceleration", N_CMoveData_m_vecContinuousAcceleration },
            { "CMoveData.Set_m_vecContinuousAcceleration", N_CMoveData_Set_m_vecContinuousAcceleration },
            { "CMoveData.m_vecWalkWishedVelocity", N_CMoveData_m_vecWalkWishedVelocity },
            { "CMoveData.Set_m_vecWalkWishedVelocity", N_CMoveData_Set_m_vecWalkWishedVelocity },
            { "CMoveData.HasZeroFrametime", N_CMoveData_HasZeroFrametime },
            { "CMoveData.IsLateCommand", N_CMoveData_IsLateCommand },
            { "CMoveData.SetHasZeroFrametime", N_CMoveData_SetHasZeroFrametime },
            { "CMoveData.SetIsLateCommand", N_CMoveData_SetIsLateCommand },
            { "CMoveData.GetSubtickMoveCount", N_CMoveData_GetSubtickMoveCount },
            { "CMoveData.GetSubtickMove", N_CMoveData_GetSubtickMove },
            // source2mod/transmit.inc
            { "Source2ModTransmit.HookEntity", N_THookEntity },
            { "Source2ModTransmit.UnhookEntity", N_TUnhookEntity },
            { "Source2ModTransmit.IsEntityHooked", N_TIsEntityHooked },
            { "Source2ModTransmit.SetVisible", N_TSetVisible },
            { "Source2ModTransmit.SetVisibleToAll", N_TSetVisibleToAll },
            { "Source2ModTransmit.IsVisible", N_TIsVisible },
            { "Source2ModTransmit.IsHiddenOnChannel", N_TIsHiddenOnChannel },
            { "Source2ModTransmit.SetBlockAll", N_TSetBlockAll },
            { "Source2ModTransmit.GetBlockAll", N_TGetBlockAll },
            { "Source2ModTransmit.SetOwner", N_TSetOwner },
            { "Source2ModTransmit.GetOwner", N_TGetOwner },
            { "Source2ModTransmit.ResetViewer", N_TResetViewer },
            { "Source2ModTransmit.HookCheckTransmit", N_THookCheckTransmit },
            { "Source2ModTransmit.UnhookCheckTransmit", N_TUnhookCheckTransmit },
            { "ISource2ModTransmitInfoList.Get", N_ListGet },
            { "ISource2ModTransmitInfo.GetPlayerSlot", N_InfoGetPlayerSlot },
            { "ISource2ModTransmitInfo.IsFullUpdate", N_InfoIsFullUpdate },
            { "ISource2ModTransmitInfo.IsTransmitting", N_InfoIsTransmitting },
            { "ISource2ModTransmitInfo.BlockTransmit", N_InfoBlockTransmit },
            { "ISource2ModTransmitInfo.AllowTransmit", N_InfoAllowTransmit },
            { "ISource2ModTransmitInfo.GetTransmitEntity", N_InfoGetBit<&ISource2ModTransmitInfo::GetTransmitEntity> },
            { "ISource2ModTransmitInfo.SetTransmitEntity", N_InfoSetBit<&ISource2ModTransmitInfo::GetTransmitEntity> },
            { "ISource2ModTransmitInfo.GetTransmitNonPlayers", N_InfoGetBit<&ISource2ModTransmitInfo::GetTransmitNonPlayers> },
            { "ISource2ModTransmitInfo.SetTransmitNonPlayers", N_InfoSetBit<&ISource2ModTransmitInfo::GetTransmitNonPlayers> },
            { "ISource2ModTransmitInfo.GetTransmitOutOfPVS", N_InfoGetBit<&ISource2ModTransmitInfo::GetTransmitOutOfPVS> },
            { "ISource2ModTransmitInfo.SetTransmitOutOfPVS", N_InfoSetBit<&ISource2ModTransmitInfo::GetTransmitOutOfPVS> },
            { "ISource2ModTransmitInfo.GetTransmitAlways", N_InfoGetBit<&ISource2ModTransmitInfo::GetTransmitAlways> },
            { "ISource2ModTransmitInfo.SetTransmitAlways", N_InfoSetBit<&ISource2ModTransmitInfo::GetTransmitAlways> },
            { nullptr, nullptr },
        };
    }

    void ForgetGameHooks(PluginId id)
    {
        std::vector<Source2ModGameHookId> ids;
        for (const auto& [hookId, hook] : g_gameHooks)
            if (hook.owner == id)
                ids.push_back(hookId);
        for (Source2ModGameHookId hookId : ids)
            Unhook(hookId);
    }

    const sp_nativeinfo_t* GetGameHookNatives()
    {
        return g_gameHookNatives;
    }
}
