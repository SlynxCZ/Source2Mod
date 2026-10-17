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
 * Hooks (source2mod/hooks.inc: HookPoint, the contexts) and transmit
 * (source2mod/transmit.inc) of the SourcePawn API mirroring the C++ one, on
 * the core's ISource2ModHooks and transmit managers.
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

#include "core/hooks.h"
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
        S2M_CTX(SpawnContext, false)
        S2M_CTX(UseContext, false)
        S2M_CTX(EntityOtherContext, false)
        S2M_CTX(ThinkContext, false)
        S2M_CTX(KilledContext, true)
        S2M_CTX(WeaponPickupContext, true)
        S2M_CTX(WeaponSwitchContext, true)
        S2M_CTX(GameFrameContext, false)
        S2M_CTX(StartupServerContext, false)
        S2M_CTX(ClientConnectContext, true)
        S2M_CTX(ClientConnectedContext, false)
        S2M_CTX(ClientPutInServerContext, false)
        S2M_CTX(ClientDisconnectContext, false)
        S2M_CTX(ClientSlotContext, false)
        S2M_CTX(EmptyContext, false)
        S2M_CTX(LoadEventsFromFileContext, false)
#undef S2M_CTX

        template <typename C>
        HandleType CtxType()
        {
            static const HandleType t = FrameType(CtxTraits<C>::name);
            return t;
        }

        template <typename C>
        HookReturn<typename C::Return> RunHandler(IPluginFunction* fn, PluginId owner, C& c, bool post)
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
                    return HookReturn<R>(action, result != 0);
                else
                    return HookReturn<R>(action, static_cast<R>(result));
            }
            else
                return action;      // the value stays what ctx.result holds
        }

        // The hooks a script made: the core sees their handlers as its own
        // code, so the owner is remembered here and checked on Unhook.
        std::map<Source2ModHookId, PluginId> g_hooks;

        template <typename C>
        Source2ModHookId AddScriptHook(IPluginFunction* fn, PluginId owner, HookPointId point, const HookTarget& target,
                                       const HookFilter& filter, bool post)
        {
            HookHandler<C> handler([fn, owner](C& c, bool p) -> HookReturn<typename C::Return>
            {
                return RunHandler(fn, owner, c, p);
            });
            return hooks::hooksManager.AddHookFor(owner, point, nullptr, target, filter, &handler, post);
        }

        Source2ModHookId AddForPoint(IPluginFunction* fn, PluginId owner, HookPointId point, const HookTarget& target,
                                     const HookFilter& filter, bool post)
        {
            switch (point)
            {
#define S2M_POINT(P, C) case HookPointId::P: return AddScriptHook<C>(fn, owner, point, target, filter, post);
                S2M_POINT(TakeDamage, TakeDamageContext)
                S2M_POINT(CanAcquire, CanAcquireContext)
                S2M_POINT(CanMove, CanMoveContext)
                S2M_POINT(CanUse, CanUseContext)
                S2M_POINT(PostThink, PostThinkContext)
                S2M_POINT(ProcessUsercmds, ProcessUsercmdsContext)
                S2M_POINT(SimulateUserCommands, SimulateUserCommandsContext)
                S2M_POINT(RunCommand, RunCommandContext)
                S2M_POINT(AcceptInput, AcceptInputContext)
                S2M_POINT(Touch, TouchContext)
                S2M_POINT(DropWeapon, DropWeaponContext)
                S2M_POINT(AirAccelerate, AirAccelerateContext)
                S2M_POINT(AirMove, MovementContext)
                S2M_POINT(CanUnduck, MovementContext)
                S2M_POINT(CategorizePosition, CategorizePositionContext)
                S2M_POINT(CheckFalling, MovementContext)
                S2M_POINT(CheckParameters, MovementContext)
                S2M_POINT(CheckVelocity, CheckVelocityContext)
                S2M_POINT(CheckWater, MovementContext)
                S2M_POINT(Duck, MovementContext)
                S2M_POINT(Friction, MovementContext)
                S2M_POINT(FullWalkMove, FullWalkMoveContext)
                S2M_POINT(GroundAccelerate, GroundAccelerateContext)
                S2M_POINT(LadderMove, MovementContext)
                S2M_POINT(MoveInit, MovementContext)
                S2M_POINT(PlayerMove, MovementContext)
                S2M_POINT(ProcessMovement, MovementContext)
                S2M_POINT(SetupMove, SetupMoveContext)
                S2M_POINT(TryPlayerMove, TryPlayerMoveContext)
                S2M_POINT(WalkMove, MovementContext)
                S2M_POINT(WaterMove, MovementContext)
                S2M_POINT(OnJumpLegacy, LegacyJumpContext)
                S2M_POINT(OnJumpModern, ModernJumpContext)
                S2M_POINT(CheckJumpButtonLegacy, LegacyJumpContext)
                S2M_POINT(CheckJumpButtonModern, ModernJumpContext)
                S2M_POINT(Spawn, SpawnContext)
                S2M_POINT(Use, UseContext)
                S2M_POINT(StartTouch, EntityOtherContext)
                S2M_POINT(EndTouch, EntityOtherContext)
                S2M_POINT(Blocked, EntityOtherContext)
                S2M_POINT(Think, ThinkContext)
                S2M_POINT(Killed, KilledContext)
                S2M_POINT(WeaponPickup, WeaponPickupContext)
                S2M_POINT(WeaponSwitch, WeaponSwitchContext)
                S2M_POINT(GameFrame, GameFrameContext)
                S2M_POINT(StartupServer, StartupServerContext)
                S2M_POINT(ClientConnect, ClientConnectContext)
                S2M_POINT(ClientConnected, ClientConnectedContext)
                S2M_POINT(ClientPutInServer, ClientPutInServerContext)
                S2M_POINT(ClientDisconnect, ClientDisconnectContext)
                S2M_POINT(ClientVoice, ClientSlotContext)
                S2M_POINT(ClientSettingsChanged, ClientSlotContext)
                S2M_POINT(GameServerSteamAPIActivated, EmptyContext)
                S2M_POINT(GameServerSteamAPIDeactivated, EmptyContext)
                S2M_POINT(LoadEventsFromFile, LoadEventsFromFileContext)
#undef S2M_POINT
            default:
                return 0;
            }
        }

        // The object behind an instance argument: an entity (handle) or a
        // SchemaObject (services, game rules, ...).
        void* InstanceArg(IPluginContext* ctx, cell_t value)
        {
            if (IsEntityCell(value))
                return EntityArg(ctx, value);
            return value ? SchemaObjectPointer(ctx, value) : nullptr;
        }

        HookFilter FilterArgs(IPluginContext* ctx, const cell_t* params, int first)
        {
            HookFilter f;
            f.designerName = params[0] >= first ? LocalString(ctx, params[first]) : "";
            f.name = params[0] >= first + 1 ? LocalString(ctx, params[first + 1]) : "";
            return f;
        }

        cell_t AddNative(IPluginContext* ctx, cell_t point, cell_t fnId, bool post, const HookTarget& target, const HookFilter& filter)
        {
            IPluginFunction* fn = Function(ctx, fnId);
            if (!fn)
                return 0;
            if (point == static_cast<cell_t>(HookPointId::Function))
                return ctx->ThrowNativeError("HookPoint_Function: use Source2ModHooks.HookFunction");
            const PluginId owner = CallerId(ctx);
            const Source2ModHookId id = AddForPoint(fn, owner, static_cast<HookPointId>(point), target, filter, post);
            if (!id)
                return ctx->ThrowNativeError("Hook point %d cannot be hooked this way (unknown point, or a target it does not take)", point);
            g_hooks[id] = owner;
            return id;
        }

        // Hook(HookPoint point, handler, bool post = false, const char[] designerName = "", const char[] name = "")
        cell_t N_HooksHook(IPluginContext* ctx, const cell_t* params)
        {
            return AddNative(ctx, params[1], params[2], params[3] != 0, HookTarget::All(), FilterArgs(ctx, params, 4));
        }

        // HookInstance(HookPoint point, any instance, handler, bool post = false, designerName = "", name = "")
        cell_t N_HooksHookInstance(IPluginContext* ctx, const cell_t* params)
        {
            void* instance = InstanceArg(ctx, params[2]);
            if (!instance)
                return ctx->ThrowNativeError("Null instance");
            return AddNative(ctx, params[1], params[3], params[4] != 0, HookTarget::Instance(instance), FilterArgs(ctx, params, 5));
        }

        // HookClass(HookPoint point, const char[] className, handler, bool post = false, designerName = "", name = "")
        cell_t N_HooksHookClass(IPluginContext* ctx, const cell_t* params)
        {
            return AddNative(ctx, params[1], params[3], params[4] != 0, HookTarget::Class(LocalString(ctx, params[2])), FilterArgs(ctx, params, 5));
        }

        bool UnhookScript(Source2ModHookId id)
        {
            g_hooks.erase(id);
            return hooks::hooksManager.Unhook(id);
        }

        cell_t N_HooksUnhook(IPluginContext* ctx, const cell_t* params)
        {
            auto it = g_hooks.find(params[1]);
            if (it == g_hooks.end())
                return UnhookFunctionHook(CallerId(ctx), params[1]);
            if (it->second != CallerId(ctx))
                return 0;
            return UnhookScript(params[1]);
        }

        cell_t N_HooksIsAvailable(IPluginContext* ctx, const cell_t* params)
        {
            return hooks::hooksManager.IsAvailable(static_cast<HookPointId>(params[1]));
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

#define S2M_CTX_STRING(C, prop, expr)                                           \
        cell_t N_##C##_##prop(IPluginContext* ctx, const cell_t* params)        \
        {                                                                       \
            C* c = Ctx<C>(ctx, params[1]);                                      \
            if (!c)                                                             \
                return 0;                                                       \
            const char* value = (expr);                                         \
            size_t written = 0;                                                 \
            ctx->StringToLocalUTF8(params[2], params[3], value ? value : "", &written); \
            return static_cast<cell_t>(written);                                \
        }
#define S2M_CTX_XUID(C)                                                         \
        cell_t N_##C##_GetXuid(IPluginContext* ctx, const cell_t* params)       \
        {                                                                       \
            C* c = Ctx<C>(ctx, params[1]);                                      \
            if (!c)                                                             \
                return 0;                                                       \
            size_t written = 0;                                                 \
            ctx->StringToLocalUTF8(params[2], params[3], std::to_string(c->xuid).c_str(), &written); \
            return static_cast<cell_t>(written);                                \
        }

        S2M_CTX_CELL(GameFrameContext, simulating, c->simulating)
        S2M_CTX_CELL(GameFrameContext, firstTick, c->firstTick)
        S2M_CTX_CELL(GameFrameContext, lastTick, c->lastTick)

        S2M_CTX_STRING(StartupServerContext, GetMapName, c->mapName)

        S2M_CTX_CELL(ClientConnectContext, slot, c->slot.Get())
        S2M_CTX_STRING(ClientConnectContext, GetName, c->name)
        S2M_CTX_XUID(ClientConnectContext)
        S2M_CTX_STRING(ClientConnectContext, GetNetworkId, c->networkId)

        // SetRejectReason(const char[] reason): what the client is told.
        cell_t N_ClientConnectContext_SetRejectReason(IPluginContext* ctx, const cell_t* params)
        {
            ClientConnectContext* c = Ctx<ClientConnectContext>(ctx, params[1]);
            if (c && c->rejectReason)
            {
                c->rejectReason->Clear();
                c->rejectReason->Insert(0, LocalString(ctx, params[2]));
            }
            return 0;
        }

        S2M_CTX_CELL(ClientConnectedContext, slot, c->slot.Get())
        S2M_CTX_STRING(ClientConnectedContext, GetName, c->name)
        S2M_CTX_XUID(ClientConnectedContext)
        S2M_CTX_STRING(ClientConnectedContext, GetNetworkId, c->networkId)
        S2M_CTX_STRING(ClientConnectedContext, GetAddress, c->address)
        S2M_CTX_CELL(ClientConnectedContext, fakePlayer, c->fakePlayer)

        S2M_CTX_CELL(ClientPutInServerContext, slot, c->slot.Get())
        S2M_CTX_STRING(ClientPutInServerContext, GetName, c->name)
        S2M_CTX_CELL(ClientPutInServerContext, type, c->type)
        S2M_CTX_XUID(ClientPutInServerContext)

        S2M_CTX_CELL(ClientDisconnectContext, slot, c->slot.Get())
        S2M_CTX_CELL(ClientDisconnectContext, reason, c->reason)
        S2M_CTX_STRING(ClientDisconnectContext, GetName, c->name)
        S2M_CTX_XUID(ClientDisconnectContext)
        S2M_CTX_STRING(ClientDisconnectContext, GetNetworkId, c->networkId)

        S2M_CTX_CELL(ClientSlotContext, slot, c->slot.Get())

        S2M_CTX_STRING(LoadEventsFromFileContext, GetFilename, c->filename)
        S2M_CTX_CELL(LoadEventsFromFileContext, searchAll, c->searchAll)


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

        // Entity virtuals.
        S2M_CTX_ENTITY(SpawnContext, entity, c->entity)
        S2M_CTX_ENTITY(UseContext, entity, c->entity)
        S2M_CTX_ENTITY(UseContext, activator, c->info ? c->info->activator : nullptr)
        S2M_CTX_ENTITY(UseContext, caller, c->info ? c->info->caller : nullptr)
        S2M_CTX_CELL(UseContext, useType, c->info ? c->info->useType : 0)
        S2M_CTX_FLOAT(UseContext, value, c->info ? c->info->value : 0.0f)
        cell_t N_UseContext_useType_set(IPluginContext* ctx, const cell_t* params)
        {
            if (UseContext* c = Ctx<UseContext>(ctx, params[1]); c && c->info)
                c->info->useType = params[2];
            return 0;
        }
        cell_t N_UseContext_value_set(IPluginContext* ctx, const cell_t* params)
        {
            if (UseContext* c = Ctx<UseContext>(ctx, params[1]); c && c->info)
                c->info->value = sp_ctof(params[2]);
            return 0;
        }
        S2M_CTX_ENTITY(EntityOtherContext, entity, c->entity)
        S2M_CTX_ENTITY(EntityOtherContext, other, c->other)
        S2M_CTX_ENTITY(ThinkContext, entity, c->entity)
        S2M_CTX_ENTITY(KilledContext, entity, c->entity)
        S2M_CTX_LENT(KilledContext, damage, DamageResultType, const_cast<CTakeDamageResult*>(c->damage))
        S2M_CTX_OBJECT(WeaponPickupContext, services, c->services)
        S2M_CTX_ENTITY(WeaponPickupContext, pawn, PawnOf(c->services))
        S2M_CTX_ENTITY(WeaponPickupContext, weapon, c->weapon)
        S2M_CTX_OBJECT(WeaponSwitchContext, services, c->services)
        S2M_CTX_ENTITY(WeaponSwitchContext, pawn, PawnOf(c->services))
        S2M_CTX_ENTITY(WeaponSwitchContext, weapon, c->weapon)
        S2M_CTX_CELL(WeaponSwitchContext, viewModelIndex, c->viewModelIndex)

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
            // source2mod/hooks.inc
            { "Source2ModHooks.Hook", N_HooksHook },
            { "Source2ModHooks.HookInstance", N_HooksHookInstance },
            { "Source2ModHooks.HookClass", N_HooksHookClass },
            { "Source2ModHooks.Unhook", N_HooksUnhook },
            { "Source2ModHooks.IsAvailable", N_HooksIsAvailable },
            { "GameFrameContext.simulating.get", N_GameFrameContext_simulating },
            { "GameFrameContext.firstTick.get", N_GameFrameContext_firstTick },
            { "GameFrameContext.lastTick.get", N_GameFrameContext_lastTick },
            { "StartupServerContext.GetMapName", N_StartupServerContext_GetMapName },
            { "ClientConnectContext.slot.get", N_ClientConnectContext_slot },
            { "ClientConnectContext.GetName", N_ClientConnectContext_GetName },
            { "ClientConnectContext.GetXuid", N_ClientConnectContext_GetXuid },
            { "ClientConnectContext.GetNetworkId", N_ClientConnectContext_GetNetworkId },
            { "ClientConnectContext.SetRejectReason", N_ClientConnectContext_SetRejectReason },
            { "ClientConnectedContext.slot.get", N_ClientConnectedContext_slot },
            { "ClientConnectedContext.GetName", N_ClientConnectedContext_GetName },
            { "ClientConnectedContext.GetXuid", N_ClientConnectedContext_GetXuid },
            { "ClientConnectedContext.GetNetworkId", N_ClientConnectedContext_GetNetworkId },
            { "ClientConnectedContext.GetAddress", N_ClientConnectedContext_GetAddress },
            { "ClientConnectedContext.fakePlayer.get", N_ClientConnectedContext_fakePlayer },
            { "ClientPutInServerContext.slot.get", N_ClientPutInServerContext_slot },
            { "ClientPutInServerContext.GetName", N_ClientPutInServerContext_GetName },
            { "ClientPutInServerContext.type.get", N_ClientPutInServerContext_type },
            { "ClientPutInServerContext.GetXuid", N_ClientPutInServerContext_GetXuid },
            { "ClientDisconnectContext.slot.get", N_ClientDisconnectContext_slot },
            { "ClientDisconnectContext.reason.get", N_ClientDisconnectContext_reason },
            { "ClientDisconnectContext.GetName", N_ClientDisconnectContext_GetName },
            { "ClientDisconnectContext.GetXuid", N_ClientDisconnectContext_GetXuid },
            { "ClientDisconnectContext.GetNetworkId", N_ClientDisconnectContext_GetNetworkId },
            { "ClientSlotContext.slot.get", N_ClientSlotContext_slot },
            { "LoadEventsFromFileContext.GetFilename", N_LoadEventsFromFileContext_GetFilename },
            { "LoadEventsFromFileContext.searchAll.get", N_LoadEventsFromFileContext_searchAll },
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
            { "SpawnContext.entity.get", N_SpawnContext_entity },
            { "SpawnContext.CallOriginal", N_CallOriginal<SpawnContext> },
            { "UseContext.entity.get", N_UseContext_entity },
            { "UseContext.activator.get", N_UseContext_activator },
            { "UseContext.caller.get", N_UseContext_caller },
            { "UseContext.useType.get", N_UseContext_useType },
            { "UseContext.useType.set", N_UseContext_useType_set },
            { "UseContext.value.get", N_UseContext_value },
            { "UseContext.value.set", N_UseContext_value_set },
            { "UseContext.CallOriginal", N_CallOriginal<UseContext> },
            { "EntityOtherContext.entity.get", N_EntityOtherContext_entity },
            { "EntityOtherContext.other.get", N_EntityOtherContext_other },
            { "EntityOtherContext.CallOriginal", N_CallOriginal<EntityOtherContext> },
            { "ThinkContext.entity.get", N_ThinkContext_entity },
            { "ThinkContext.CallOriginal", N_CallOriginal<ThinkContext> },
            { "KilledContext.entity.get", N_KilledContext_entity },
            { "KilledContext.damage.get", N_KilledContext_damage },
            { "KilledContext.CallOriginal", N_CallOriginal<KilledContext> },
            { "WeaponPickupContext.services.get", N_WeaponPickupContext_services },
            { "WeaponPickupContext.pawn.get", N_WeaponPickupContext_pawn },
            { "WeaponPickupContext.weapon.get", N_WeaponPickupContext_weapon },
            { "WeaponPickupContext.CallOriginal", N_CallOriginal<WeaponPickupContext> },
            { "WeaponSwitchContext.services.get", N_WeaponSwitchContext_services },
            { "WeaponSwitchContext.pawn.get", N_WeaponSwitchContext_pawn },
            { "WeaponSwitchContext.weapon.get", N_WeaponSwitchContext_weapon },
            { "WeaponSwitchContext.viewModelIndex.get", N_WeaponSwitchContext_viewModelIndex },
            { "WeaponSwitchContext.CallOriginal", N_CallOriginal<WeaponSwitchContext> },
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
        std::vector<Source2ModHookId> ids;
        for (const auto& [hookId, owner] : g_hooks)
            if (owner == id)
                ids.push_back(hookId);
        for (Source2ModHookId hookId : ids)
            UnhookScript(hookId);
    }

    const sp_nativeinfo_t* GetGameHookNatives()
    {
        return g_gameHookNatives;
    }
}
