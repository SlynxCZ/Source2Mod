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
 * SourcePawn natives for the core's smaller services: Source2ModPaths,
 * Source2ModPermissions, Source2ModTrace (TraceOptions/TraceResult),
 * Source2ModGameSystems and Source2ModAddresses (source2mod/paths.inc,
 * permissions.inc, trace.inc, gamesystems.inc, addresses.inc).
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"
#include "addresses_list.h"

#include "core/addresses.h"
#include "core/gamesystems.h"
#include "core/paths.h"
#include "core/permissions.h"
#include "core/raytrace.h"

#include <source2mod/schema/navarea.h>
#include <source2mod/schema/entity/classes/CBaseEntity.h>

#include <sp_callable.h>

#include <cstdlib>
#include <map>
#include <string>
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

        cell_t ToBuffer(IPluginContext* ctx, cell_t addr, cell_t maxlen, const char* value)
        {
            size_t written = 0;
            ctx->StringToLocalUTF8(addr, maxlen, value ? value : "", &written);
            return static_cast<cell_t>(written);
        }

        /// An entity argument that may be NULL_ENTITY.
        bool OptionalEntity(IPluginContext* ctx, cell_t entity, CBaseEntity** out)
        {
            bool ok;
            *out = static_cast<CBaseEntity*>(OptionalEntityArg(ctx, entity, &ok));
            return ok;
        }

        Vector ReadVector(IPluginContext* ctx, cell_t addr)
        {
            cell_t* v;
            ctx->LocalToPhysAddr(addr, &v);
            return Vector(sp_ctof(v[0]), sp_ctof(v[1]), sp_ctof(v[2]));
        }

        void WriteVector(IPluginContext* ctx, cell_t addr, const Vector& in)
        {
            cell_t* out;
            ctx->LocalToPhysAddr(addr, &out);
            out[0] = sp_ftoc(in.x);
            out[1] = sp_ftoc(in.y);
            out[2] = sp_ftoc(in.z);
        }

        uint64_t ReadMask(IPluginContext* ctx, cell_t addr)
        {
            cell_t* m;
            ctx->LocalToPhysAddr(addr, &m);
            return static_cast<uint32_t>(m[0]) | (static_cast<uint64_t>(static_cast<uint32_t>(m[1])) << 32);
        }

        void WriteMask(IPluginContext* ctx, cell_t addr, uint64_t mask)
        {
            cell_t* m;
            ctx->LocalToPhysAddr(addr, &m);
            m[0] = static_cast<cell_t>(static_cast<uint32_t>(mask));
            m[1] = static_cast<cell_t>(static_cast<uint32_t>(mask >> 32));
        }

        // ---- Source2ModPaths -------------------------------------------------------

#define S2M_PATH_NATIVE(name)                                                       \
        cell_t N_Paths##name(IPluginContext* ctx, const cell_t* params)             \
        {                                                                           \
            return ToBuffer(ctx, params[1], params[2], paths::pathsManager.name()); \
        }
        S2M_PATH_NATIVE(GameDirectory)
        S2M_PATH_NATIVE(RootDirectory)
        S2M_PATH_NATIVE(ConfigsDirectory)
        S2M_PATH_NATIVE(GamedataDirectory)
        S2M_PATH_NATIVE(PluginsDirectory)
        S2M_PATH_NATIVE(SharedDirectory)
#undef S2M_PATH_NATIVE

        cell_t N_PathsPluginConfigDirectory(IPluginContext* ctx, const cell_t* params)
        {
            return ToBuffer(ctx, params[1], params[2], paths::pathsManager.PluginConfigDirectory(CallerId(ctx)));
        }

        // ---- Source2ModPermissions -------------------------------------------------

        uint64 SteamId(IPluginContext* ctx, cell_t addr)
        {
            return std::strtoull(LocalString(ctx, addr), nullptr, 10);
        }

        CPlayerSlot Slot(cell_t value)
        {
            return CPlayerSlot(value);
        }

        auto& Perms()
        {
            return permissions::permissionsManager;
        }

        cell_t N_PermHasPermission(IPluginContext* ctx, const cell_t* params)
        {
            return Perms().HasPermission(SteamId(ctx, params[1]), LocalString(ctx, params[2]));
        }

        cell_t N_PermPlayerHasPermission(IPluginContext* ctx, const cell_t* params)
        {
            return Perms().PlayerHasPermission(Slot(params[1]), LocalString(ctx, params[2]));
        }

        cell_t N_PermCheckAccess(IPluginContext* ctx, const cell_t* params)
        {
            return Perms().CheckAccess(Slot(params[1]), LocalString(ctx, params[2]), LocalString(ctx, params[3]));
        }

        cell_t N_PermInGroup(IPluginContext* ctx, const cell_t* params)
        {
            return Perms().InGroup(SteamId(ctx, params[1]), LocalString(ctx, params[2]));
        }

        cell_t N_PermGetImmunity(IPluginContext* ctx, const cell_t* params)
        {
            return Perms().GetImmunity(SteamId(ctx, params[1]));
        }

        cell_t N_PermCanTarget(IPluginContext*, const cell_t* params)
        {
            return Perms().CanTarget(Slot(params[1]), Slot(params[2]));
        }

        cell_t N_PermGetPlayerSteamID(IPluginContext* ctx, const cell_t* params)
        {
            const std::string text = std::to_string(Perms().GetPlayerSteamID(Slot(params[1])));
            return ToBuffer(ctx, params[2], params[3], text.c_str());
        }

        cell_t N_PermIsPlayerAuthorized(IPluginContext*, const cell_t* params)
        {
            return Perms().IsPlayerAuthorized(Slot(params[1]));
        }

        cell_t N_PermGrantPermission(IPluginContext* ctx, const cell_t* params)
        {
            Perms().GrantPermission(CallerId(ctx), SteamId(ctx, params[1]), LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_PermRevokePermission(IPluginContext* ctx, const cell_t* params)
        {
            Perms().RevokePermission(CallerId(ctx), SteamId(ctx, params[1]), LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_PermAddToGroup(IPluginContext* ctx, const cell_t* params)
        {
            Perms().AddToGroup(CallerId(ctx), SteamId(ctx, params[1]), LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_PermRemoveFromGroup(IPluginContext* ctx, const cell_t* params)
        {
            Perms().RemoveFromGroup(CallerId(ctx), SteamId(ctx, params[1]), LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_PermSetImmunity(IPluginContext* ctx, const cell_t* params)
        {
            Perms().SetImmunity(CallerId(ctx), SteamId(ctx, params[1]), params[2]);
            return 0;
        }

        cell_t N_PermClearPlayer(IPluginContext* ctx, const cell_t* params)
        {
            Perms().ClearPlayer(CallerId(ctx), SteamId(ctx, params[1]));
            return 0;
        }

        cell_t N_PermCreateGroup(IPluginContext* ctx, const cell_t* params)
        {
            Perms().CreateGroup(CallerId(ctx), LocalString(ctx, params[1]), params[2]);
            return 0;
        }

        cell_t N_PermDeleteGroup(IPluginContext* ctx, const cell_t* params)
        {
            Perms().DeleteGroup(CallerId(ctx), LocalString(ctx, params[1]));
            return 0;
        }

        cell_t N_PermAddGroupPermission(IPluginContext* ctx, const cell_t* params)
        {
            Perms().AddGroupPermission(CallerId(ctx), LocalString(ctx, params[1]), LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_PermRemoveGroupPermission(IPluginContext* ctx, const cell_t* params)
        {
            Perms().RemoveGroupPermission(CallerId(ctx), LocalString(ctx, params[1]), LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_PermAddGroupParent(IPluginContext* ctx, const cell_t* params)
        {
            Perms().AddGroupParent(CallerId(ctx), LocalString(ctx, params[1]), LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_PermRemoveGroupParent(IPluginContext* ctx, const cell_t* params)
        {
            Perms().RemoveGroupParent(CallerId(ctx), LocalString(ctx, params[1]), LocalString(ctx, params[2]));
            return 0;
        }

        // Access-denied handler ids per plugin; cleared when it unloads.
        std::map<Source2ModHookId, PluginId> g_deniedHandlers;

        cell_t N_PermSetAccessDeniedHandler(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[1]);
            if (!fn)
                return 0;
            const PluginId owner = CallerId(ctx);
            AccessDeniedHandler handler([fn, owner](const Source2ModCommandContext& c, const Source2ModCommandArgs& a, const char* permission)
            {
                const cell_t context = LendCommandContext(&c, owner);
                const cell_t args = LendCommandArgs(&a, owner);
                sp::CallArgs call;
                call.PushCell(context);
                call.PushCell(args);
                call.PushString(permission ? permission : "");
                Invoke(fn, call);
                Handles().Free(args, 0, true);
                Handles().Free(context, 0, true);
            });
            const Source2ModHookId id = Perms().SetAccessDeniedHandler(std::move(handler));
            if (id)
                g_deniedHandlers[id] = owner;
            return static_cast<cell_t>(id);
        }

        cell_t N_PermClearAccessDeniedHandler(IPluginContext* ctx, const cell_t* params)
        {
            auto it = g_deniedHandlers.find(params[1]);
            if (it == g_deniedHandlers.end() || it->second != CallerId(ctx))
                return 0;
            g_deniedHandlers.erase(it);
            return Perms().ClearAccessDeniedHandler(params[1]);
        }

        // ---- TraceOptions / TraceResult --------------------------------------------

        struct ScriptTraceOptions : HandleObject
        {
            TraceOptions options;
        };

        struct ScriptTraceResult : HandleObject
        {
            explicit ScriptTraceResult(TraceResult&& r) : result(std::move(r)) {}
            TraceResult result;
        };

        HandleType OptionsType()
        {
            static const HandleType t = Handles().CreateType("TraceOptions", [](void* p) { delete static_cast<ScriptTraceOptions*>(p); });
            return t;
        }

        HandleType ResultType()
        {
            static const HandleType t = Handles().CreateType("TraceResult", [](void* p) { delete static_cast<ScriptTraceResult*>(p); });
            return t;
        }

        TraceOptions* OptionsOf(IPluginContext* ctx, cell_t handle)
        {
            auto* o = static_cast<ScriptTraceOptions*>(ReadHandle(ctx, handle, OptionsType()));
            return o ? &o->options : nullptr;
        }

        const TraceResult* ResultOf(IPluginContext* ctx, cell_t handle)
        {
            auto* r = static_cast<ScriptTraceResult*>(ReadHandle(ctx, handle, ResultType()));
            return r ? &r->result : nullptr;
        }

        cell_t N_OptionsNew(IPluginContext* ctx, const cell_t*)
        {
            auto* o = new ScriptTraceOptions();
            const cell_t h = Handles().Create(OptionsType(), o, CallerId(ctx));
            if (!h)
                delete o;
            return h;
        }

#define S2M_MASK_NATIVES(field)                                                    \
        cell_t N_OptionsSet##field(IPluginContext* ctx, const cell_t* params)      \
        {                                                                          \
            if (TraceOptions* o = OptionsOf(ctx, params[1]))                       \
                o->field = ReadMask(ctx, params[2]);                               \
            return 0;                                                              \
        }                                                                          \
        cell_t N_OptionsGet##field(IPluginContext* ctx, const cell_t* params)      \
        {                                                                          \
            if (TraceOptions* o = OptionsOf(ctx, params[1]))                       \
                WriteMask(ctx, params[2], o->field);                               \
            return 0;                                                              \
        }
        S2M_MASK_NATIVES(InteractsAs)
        S2M_MASK_NATIVES(InteractsWith)
        S2M_MASK_NATIVES(InteractsExclude)
#undef S2M_MASK_NATIVES

#define S2M_RESULT_VECTOR(name)                                                    \
        cell_t N_Result##name(IPluginContext* ctx, const cell_t* params)           \
        {                                                                          \
            if (const TraceResult* r = ResultOf(ctx, params[1]))                   \
                WriteVector(ctx, params[2], r->name());                            \
            return 0;                                                              \
        }
        S2M_RESULT_VECTOR(StartPos)
        S2M_RESULT_VECTOR(EndPos)
        S2M_RESULT_VECTOR(HitPoint)
        S2M_RESULT_VECTOR(Normal)
#undef S2M_RESULT_VECTOR

#define S2M_RESULT_VALUE(name, conv)                                               \
        cell_t N_Result##name(IPluginContext* ctx, const cell_t* params)           \
        {                                                                          \
            const TraceResult* r = ResultOf(ctx, params[1]);                       \
            return r ? conv(r->name()) : 0;                                        \
        }
#define S2M_CELL(v) static_cast<cell_t>(v)
        S2M_RESULT_VALUE(Fraction, sp_ftoc)
        S2M_RESULT_VALUE(HitOffset, sp_ftoc)
        S2M_RESULT_VALUE(DidHit, S2M_CELL)
        S2M_RESULT_VALUE(IsAllSolid, S2M_CELL)
        S2M_RESULT_VALUE(HasExactHit, S2M_CELL)
        S2M_RESULT_VALUE(TriangleIndex, S2M_CELL)
        S2M_RESULT_VALUE(HitboxBoneIndex, S2M_CELL)
        S2M_RESULT_VALUE(Contents, S2M_CELL)
        S2M_RESULT_VALUE(RayType, S2M_CELL)
#undef S2M_CELL
#undef S2M_RESULT_VALUE

        cell_t N_ResultHitEntity(IPluginContext* ctx, const cell_t* params)
        {
            const TraceResult* r = ResultOf(ctx, params[1]);
            return r ? EntityCell(r->HitEntity()) : -1;
        }

        cell_t NewResult(IPluginContext* ctx, TraceResult&& result)
        {
            auto* r = new ScriptTraceResult(std::move(result));
            const cell_t h = Handles().Create(ResultType(), r, CallerId(ctx));
            if (!h)
                delete r;
            return h;
        }

        /// The ignore entity and options of a trace call; false after a script error.
        bool TraceArgs(IPluginContext* ctx, cell_t ignore, cell_t options, CBaseEntity** entity, TraceOptions** opts)
        {
            if (!OptionalEntity(ctx, ignore, entity))
                return false;
            *opts = nullptr;
            if (options)
            {
                *opts = OptionsOf(ctx, options);
                if (!*opts)
                    return false;
            }
            return true;
        }

        // TraceShape(start, angles, ignore, options)
        cell_t N_TraceShape(IPluginContext* ctx, const cell_t* params)
        {
            CBaseEntity* ignore;
            TraceOptions* opts;
            if (!TraceArgs(ctx, params[3], params[4], &ignore, &opts))
                return 0;
            const Vector a = ReadVector(ctx, params[2]);
            return NewResult(ctx, raytrace::rayTrace.TraceShape(ReadVector(ctx, params[1]), QAngle(a.x, a.y, a.z), ignore, opts));
        }

        // TraceEndShape(start, end, ignore, options)
        cell_t N_TraceEndShape(IPluginContext* ctx, const cell_t* params)
        {
            CBaseEntity* ignore;
            TraceOptions* opts;
            if (!TraceArgs(ctx, params[3], params[4], &ignore, &opts))
                return 0;
            return NewResult(ctx, raytrace::rayTrace.TraceEndShape(ReadVector(ctx, params[1]), ReadVector(ctx, params[2]), ignore, opts));
        }

        // TraceHullShape(start, end, mins, maxs, ignore, options)
        cell_t N_TraceHullShape(IPluginContext* ctx, const cell_t* params)
        {
            CBaseEntity* ignore;
            TraceOptions* opts;
            if (!TraceArgs(ctx, params[5], params[6], &ignore, &opts))
                return 0;
            return NewResult(ctx, raytrace::rayTrace.TraceHullShape(ReadVector(ctx, params[1]), ReadVector(ctx, params[2]),
                                                                    ReadVector(ctx, params[3]), ReadVector(ctx, params[4]), ignore, opts));
        }

        // PointContents(pos, mask[2], contents[2])
        cell_t N_TracePointContents(IPluginContext* ctx, const cell_t* params)
        {
            const Vector pos = ReadVector(ctx, params[1]);
            WriteMask(ctx, params[3], raytrace::rayTrace.PointContents(&pos, ReadMask(ctx, params[2])));
            return 0;
        }

        // GetEntityWorldSpaceAABB(entity, mins, maxs)
        cell_t N_TraceGetEntityWorldSpaceAABB(IPluginContext* ctx, const cell_t* params)
        {
            CBaseEntity* entity;
            if (!OptionalEntity(ctx, params[1], &entity))
                return 0;
            if (!entity)
                return ctx->ThrowNativeError("Entity %d is invalid", params[1]);
            Vector mins, maxs;
            raytrace::rayTrace.GetEntityWorldSpaceAABB(entity, &mins, &maxs);
            WriteVector(ctx, params[2], mins);
            WriteVector(ctx, params[3], maxs);
            return 0;
        }

        // TraceShapeEx's filter: the ignore entity as TraceEndShape's, then the
        // script's function for every entity the ray touches.
        class ScriptTraceFilter final : public CTraceFilterEx
        {
        public:
            ScriptTraceFilter(CBaseEntity* ignore, IPluginFunction* fn, cell_t data)
                : CTraceFilterEx(ignore), m_fn(fn), m_data(data)
            {
                m_bIterateEntities = fn != nullptr;
            }

            ScriptTraceFilter(IPluginFunction* fn, cell_t data) : m_fn(fn), m_data(data)
            {
                m_bIterateEntities = fn != nullptr;
            }

            bool ShouldHitEntity(CEntityInstance* entity) override
            {
                if (!m_fn)
                    return true;
                sp::CallArgs args;
                args.PushCell(EntityCell(entity));
                args.PushCell(m_data);
                cell_t res = 1;
                Invoke(m_fn, args, &res);
                return res != 0;
            }

        private:
            IPluginFunction* m_fn;
            cell_t m_data;
        };

        // TraceShapeEx(start, end, options, ignore, filter, data, rayType, a, b, radius)
        cell_t N_TraceShapeEx(IPluginContext* ctx, const cell_t* params)
        {
            CBaseEntity* ignore;
            TraceOptions* opts;
            if (!TraceArgs(ctx, params[4], params[3], &ignore, &opts))
                return 0;
            IPluginFunction* fn = nullptr;
            if (params[5] != 0 && params[5] != -1)
            {
                fn = ctx->GetFunctionById(static_cast<funcid_t>(params[5]));
                if (!fn)
                    return ctx->ThrowNativeError("Invalid function id (%X)", params[5]);
            }
            ScriptTraceFilter filter = ignore ? ScriptTraceFilter(ignore, fn, params[6]) : ScriptTraceFilter(fn, params[6]);
            filter.m_nInteractsAs = opts ? opts->InteractsAs : CONTENTS_EMPTY;
            filter.m_nInteractsWith = opts ? opts->InteractsWith : MASK_ALL;
            filter.m_nInteractsExclude = opts ? opts->InteractsExclude : CONTENTS_EMPTY;

            const Vector a = ReadVector(ctx, params[8]);
            const Vector b = ReadVector(ctx, params[9]);
            const float radius = sp_ctof(params[10]);
            Ray_t ray;
            switch (params[7])
            {
            case RAY_TYPE_SPHERE:
                ray.Init(a, radius);
                break;
            case RAY_TYPE_HULL:
                ray.Init(a, b);
                break;
            case RAY_TYPE_CAPSULE:
                ray.Init(a, b, radius);
                break;
            default:
                break;
            }
            return NewResult(ctx, raytrace::rayTrace.TraceShapeEx(ReadVector(ctx, params[1]), ReadVector(ctx, params[2]), &filter, &ray));
        }

        // ---- TraceResult: hitbox, surface, shape, body ------------------------------

        cell_t WriteString(IPluginContext* ctx, cell_t addr, cell_t maxlen, const char* value)
        {
            size_t written = 0;
            ctx->StringToLocalUTF8(addr, maxlen, value ? value : "", &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_ResultHitGroup(IPluginContext* ctx, const cell_t* params)
        {
            const TraceResult* r = ResultOf(ctx, params[1]);
            return r && r->Hitbox() ? r->Hitbox()->m_nGroupId : -1;
        }

        cell_t N_ResultHitboxIndex(IPluginContext* ctx, const cell_t* params)
        {
            const TraceResult* r = ResultOf(ctx, params[1]);
            return r && r->Hitbox() ? r->Hitbox()->m_nHitBoxIndex : -1;
        }

        cell_t N_ResultHitboxName(IPluginContext* ctx, const cell_t* params)
        {
            const TraceResult* r = ResultOf(ctx, params[1]);
            return WriteString(ctx, params[2], params[3], r && r->Hitbox() ? r->Hitbox()->m_name.Get() : "");
        }

        cell_t N_ResultHitboxBoneName(IPluginContext* ctx, const cell_t* params)
        {
            const TraceResult* r = ResultOf(ctx, params[1]);
            return WriteString(ctx, params[2], params[3], r && r->Hitbox() ? r->Hitbox()->m_sBoneName.Get() : "");
        }

        cell_t N_ResultSurfaceName(IPluginContext* ctx, const cell_t* params)
        {
            const TraceResult* r = ResultOf(ctx, params[1]);
            return WriteString(ctx, params[2], params[3], r && r->Surface() ? r->Surface()->m_name.Get() : "");
        }

        // SurfaceProperties(float physics[6]): friction, elasticity, density, thickness, soft contact frequency, damping ratio.
        cell_t N_ResultSurfacePhysics(IPluginContext* ctx, const cell_t* params)
        {
            const TraceResult* r = ResultOf(ctx, params[1]);
            if (!r || !r->Surface())
                return 0;
            const CPhysSurfacePropertiesPhysics& p = r->Surface()->m_physics;
            cell_t* out;
            ctx->LocalToPhysAddr(params[2], &out);
            out[0] = sp_ftoc(p.m_friction);
            out[1] = sp_ftoc(p.m_elasticity);
            out[2] = sp_ftoc(p.m_density);
            out[3] = sp_ftoc(p.m_thickness);
            out[4] = sp_ftoc(p.m_softContactFrequency);
            out[5] = sp_ftoc(p.m_softContactDampingRatio);
            return 1;
        }

#define S2M_SHAPE_MASK(name, field)                                                \
        cell_t N_ResultShape##name(IPluginContext* ctx, const cell_t* params)      \
        {                                                                          \
            if (const TraceResult* r = ResultOf(ctx, params[1]))                   \
                WriteMask(ctx, params[2], r->ShapeAttributes().field);             \
            return 0;                                                              \
        }
        S2M_SHAPE_MASK(InteractsAs, m_nInteractsAs)
        S2M_SHAPE_MASK(InteractsWith, m_nInteractsWith)
        S2M_SHAPE_MASK(InteractsExclude, m_nInteractsExclude)
#undef S2M_SHAPE_MASK

#define S2M_SHAPE_VALUE(name, field)                                               \
        cell_t N_ResultShape##name(IPluginContext* ctx, const cell_t* params)      \
        {                                                                          \
            const TraceResult* r = ResultOf(ctx, params[1]);                       \
            return r ? static_cast<cell_t>(r->ShapeAttributes().field) : 0;        \
        }
        S2M_SHAPE_VALUE(CollisionGroup, m_nCollisionGroup)
        S2M_SHAPE_VALUE(CollisionFunctionMask, m_nCollisionFunctionMask)
        S2M_SHAPE_VALUE(HierarchyId, m_nHierarchyId)
#undef S2M_SHAPE_VALUE

        // BodyTransform(float position[3], float orientation[4])
        cell_t N_ResultBodyTransform(IPluginContext* ctx, const cell_t* params)
        {
            const TraceResult* r = ResultOf(ctx, params[1]);
            if (!r)
                return 0;
            const CTransform& t = r->BodyTransform();
            WriteVector(ctx, params[2], Vector(t.m_vPosition.x, t.m_vPosition.y, t.m_vPosition.z));
            cell_t* q;
            ctx->LocalToPhysAddr(params[3], &q);
            q[0] = sp_ftoc(t.m_orientation.x);
            q[1] = sp_ftoc(t.m_orientation.y);
            q[2] = sp_ftoc(t.m_orientation.z);
            q[3] = sp_ftoc(t.m_orientation.w);
            return 0;
        }

        // ---- CCSNavArea (SchemaObjects: an area stays valid for the map) -----------------

        CCSNavArea* AreaOf(IPluginContext* ctx, cell_t handle)
        {
            if (handle == 0)
            {
                ctx->ThrowNativeError("Null nav area");
                return nullptr;
            }
            return static_cast<CCSNavArea*>(SchemaObjectPointer(ctx, handle));
        }

        cell_t N_NavId(IPluginContext* ctx, const cell_t* params)
        {
            const CCSNavArea* a = AreaOf(ctx, params[1]);
            return a ? static_cast<cell_t>(a->Id()) : 0;
        }

        template <Vector (*GET)(const CCSNavArea*)>
        cell_t N_NavVector(IPluginContext* ctx, const cell_t* params)
        {
            if (const CCSNavArea* a = AreaOf(ctx, params[1]))
                WriteVector(ctx, params[2], GET(a));
            return 0;
        }

        Vector NavCenter(const CCSNavArea* a) { return a->Center(); }
        Vector NavNormal(const CCSNavArea* a) { return a->Normal(); }
        Vector NavMin(const CCSNavArea* a) { return a->Min(); }
        Vector NavMax(const CCSNavArea* a) { return a->Max(); }

        template <float (CCSNavArea::*GET)() const>
        cell_t N_NavFloat(IPluginContext* ctx, const cell_t* params)
        {
            const CCSNavArea* a = AreaOf(ctx, params[1]);
            return a ? sp_ftoc((a->*GET)()) : 0;
        }

        cell_t N_NavGetHeightAtPosition(IPluginContext* ctx, const cell_t* params)
        {
            const CCSNavArea* a = AreaOf(ctx, params[1]);
            return a ? sp_ftoc(a->GetHeightAtPosition(sp_ctof(params[2]), sp_ctof(params[3]))) : 0;
        }

        cell_t N_NavContainsPoint(IPluginContext* ctx, const cell_t* params)
        {
            const CCSNavArea* a = AreaOf(ctx, params[1]);
            return a && a->ContainsPoint(ReadVector(ctx, params[2]), sp_ctof(params[3]));
        }

        cell_t N_NavContainsBox(IPluginContext* ctx, const cell_t* params)
        {
            const CCSNavArea* a = AreaOf(ctx, params[1]);
            return a && a->ContainsBox(ReadVector(ctx, params[2]), ReadVector(ctx, params[3]), sp_ctof(params[4]));
        }

        cell_t N_NavIntersectsBox(IPluginContext* ctx, const cell_t* params)
        {
            const CCSNavArea* a = AreaOf(ctx, params[1]);
            return a && a->IntersectsBox(ReadVector(ctx, params[2]), ReadVector(ctx, params[3]), sp_ctof(params[4]));
        }

        cell_t N_NavGetClosestPoint(IPluginContext* ctx, const cell_t* params)
        {
            if (const CCSNavArea* a = AreaOf(ctx, params[1]))
                WriteVector(ctx, params[3], a->GetClosestPoint(ReadVector(ctx, params[2])));
            return 0;
        }

        cell_t N_NavGetDistanceToPoint(IPluginContext* ctx, const cell_t* params)
        {
            const CCSNavArea* a = AreaOf(ctx, params[1]);
            return a ? sp_ftoc(a->GetDistanceToPoint(ReadVector(ctx, params[2]))) : 0;
        }

        cell_t N_NavGetDistanceSquaredToPoint(IPluginContext* ctx, const cell_t* params)
        {
            const CCSNavArea* a = AreaOf(ctx, params[1]);
            return a ? sp_ftoc(a->GetDistanceSquaredToPoint(ReadVector(ctx, params[2]))) : 0;
        }

        // GetAllNavAreas() -> ArrayList of CCSNavArea
        cell_t N_NavGetAllNavAreas(IPluginContext* ctx, const cell_t*)
        {
            std::vector<cell_t> areas;
            for (CCSNavArea* a : CCSNavArea::GetAllNavAreas())
                areas.push_back(SchemaObjectForPointer(a));
            return NewCellList(areas, CallerId(ctx));
        }

        // GetClosestNavArea(pos, maxDistance = -1.0, &distance)
        cell_t N_NavGetClosestNavArea(IPluginContext* ctx, const cell_t* params)
        {
            float distance = 0.0f;
            CCSNavArea* a = CCSNavArea::GetClosestNavArea(ReadVector(ctx, params[1]), &distance, sp_ctof(params[2]));
            if (params[0] >= 3)
            {
                cell_t* out;
                ctx->LocalToPhysAddr(params[3], &out);
                *out = sp_ftoc(distance);
            }
            return SchemaObjectForPointer(a);
        }

        cell_t N_NavIsValidNavMesh(IPluginContext*, const cell_t*)
        {
            return CCSNavArea::IsValidNavMesh();
        }

        // Source2ModTrace.CheckAreaOverlappingEntity(area, entity, extrudeHullHeight)
        cell_t N_TraceCheckAreaOverlappingEntity(IPluginContext* ctx, const cell_t* params)
        {
            const CCSNavArea* a = AreaOf(ctx, params[1]);
            CBaseEntity* entity;
            if (!a || !OptionalEntity(ctx, params[2], &entity))
                return 0;
            if (!entity)
                return ctx->ThrowNativeError("Entity %d is invalid", params[2]);
            return raytrace::rayTrace.CheckAreaOverlappingEntity(a, entity, params[3] != 0);
        }

        // ---- Source2ModGameSystems -------------------------------------------------

        cell_t N_GameSystemsIsAvailable(IPluginContext*, const cell_t*)
        {
            return gamesystems::gameSystemsManager.IsAvailable();
        }

        cell_t N_GameSystemsFindByName(IPluginContext* ctx, const cell_t* params)
        {
            return SchemaObjectForPointer(gamesystems::gameSystemsManager.FindByName(LocalString(ctx, params[1])));
        }

        // ---- Source2ModAddresses ---------------------------------------------------

        cell_t N_AddressesIsResolved(IPluginContext* ctx, const cell_t* params)
        {
            return AddressOf(ctx, params[1]) != nullptr;
        }

        const sp_nativeinfo_t g_worldNatives[] = {
            { "Source2ModPaths.GameDirectory", N_PathsGameDirectory },
            { "Source2ModPaths.RootDirectory", N_PathsRootDirectory },
            { "Source2ModPaths.ConfigsDirectory", N_PathsConfigsDirectory },
            { "Source2ModPaths.GamedataDirectory", N_PathsGamedataDirectory },
            { "Source2ModPaths.PluginsDirectory", N_PathsPluginsDirectory },
            { "Source2ModPaths.SharedDirectory", N_PathsSharedDirectory },
            { "Source2ModPaths.PluginConfigDirectory", N_PathsPluginConfigDirectory },

            { "Source2ModPermissions.HasPermission", N_PermHasPermission },
            { "Source2ModPermissions.PlayerHasPermission", N_PermPlayerHasPermission },
            { "Source2ModPermissions.CheckAccess", N_PermCheckAccess },
            { "Source2ModPermissions.InGroup", N_PermInGroup },
            { "Source2ModPermissions.GetImmunity", N_PermGetImmunity },
            { "Source2ModPermissions.CanTarget", N_PermCanTarget },
            { "Source2ModPermissions.GetPlayerSteamID", N_PermGetPlayerSteamID },
            { "Source2ModPermissions.IsPlayerAuthorized", N_PermIsPlayerAuthorized },
            { "Source2ModPermissions.GrantPermission", N_PermGrantPermission },
            { "Source2ModPermissions.RevokePermission", N_PermRevokePermission },
            { "Source2ModPermissions.AddToGroup", N_PermAddToGroup },
            { "Source2ModPermissions.RemoveFromGroup", N_PermRemoveFromGroup },
            { "Source2ModPermissions.SetImmunity", N_PermSetImmunity },
            { "Source2ModPermissions.ClearPlayer", N_PermClearPlayer },
            { "Source2ModPermissions.CreateGroup", N_PermCreateGroup },
            { "Source2ModPermissions.DeleteGroup", N_PermDeleteGroup },
            { "Source2ModPermissions.AddGroupPermission", N_PermAddGroupPermission },
            { "Source2ModPermissions.RemoveGroupPermission", N_PermRemoveGroupPermission },
            { "Source2ModPermissions.AddGroupParent", N_PermAddGroupParent },
            { "Source2ModPermissions.RemoveGroupParent", N_PermRemoveGroupParent },
            { "Source2ModPermissions.SetAccessDeniedHandler", N_PermSetAccessDeniedHandler },
            { "Source2ModPermissions.ClearAccessDeniedHandler", N_PermClearAccessDeniedHandler },

            { "TraceOptions.TraceOptions", N_OptionsNew },
            { "TraceOptions.SetInteractsAs", N_OptionsSetInteractsAs },
            { "TraceOptions.SetInteractsWith", N_OptionsSetInteractsWith },
            { "TraceOptions.SetInteractsExclude", N_OptionsSetInteractsExclude },
            { "TraceOptions.GetInteractsAs", N_OptionsGetInteractsAs },
            { "TraceOptions.GetInteractsWith", N_OptionsGetInteractsWith },
            { "TraceOptions.GetInteractsExclude", N_OptionsGetInteractsExclude },
            { "TraceResult.StartPos", N_ResultStartPos },
            { "TraceResult.EndPos", N_ResultEndPos },
            { "TraceResult.HitPoint", N_ResultHitPoint },
            { "TraceResult.Normal", N_ResultNormal },
            { "TraceResult.Fraction", N_ResultFraction },
            { "TraceResult.HitOffset", N_ResultHitOffset },
            { "TraceResult.DidHit", N_ResultDidHit },
            { "TraceResult.IsAllSolid", N_ResultIsAllSolid },
            { "TraceResult.HasExactHit", N_ResultHasExactHit },
            { "TraceResult.HitEntity", N_ResultHitEntity },
            { "TraceResult.TriangleIndex", N_ResultTriangleIndex },
            { "TraceResult.HitboxBoneIndex", N_ResultHitboxBoneIndex },
            { "TraceResult.Contents", N_ResultContents },
            { "TraceResult.RayType", N_ResultRayType },
            { "Source2ModTrace.TraceShape", N_TraceShape },
            { "Source2ModTrace.TraceEndShape", N_TraceEndShape },
            { "Source2ModTrace.TraceHullShape", N_TraceHullShape },
            { "Source2ModTrace.PointContents", N_TracePointContents },
            { "Source2ModTrace.GetEntityWorldSpaceAABB", N_TraceGetEntityWorldSpaceAABB },
            { "Source2ModTrace.TraceShapeEx", N_TraceShapeEx },
            { "Source2ModTrace.CheckAreaOverlappingEntity", N_TraceCheckAreaOverlappingEntity },
            { "TraceResult.HitGroup", N_ResultHitGroup },
            { "TraceResult.HitboxIndex", N_ResultHitboxIndex },
            { "TraceResult.HitboxName", N_ResultHitboxName },
            { "TraceResult.HitboxBoneName", N_ResultHitboxBoneName },
            { "TraceResult.SurfaceName", N_ResultSurfaceName },
            { "TraceResult.SurfacePhysics", N_ResultSurfacePhysics },
            { "TraceResult.ShapeInteractsAs", N_ResultShapeInteractsAs },
            { "TraceResult.ShapeInteractsWith", N_ResultShapeInteractsWith },
            { "TraceResult.ShapeInteractsExclude", N_ResultShapeInteractsExclude },
            { "TraceResult.ShapeCollisionGroup", N_ResultShapeCollisionGroup },
            { "TraceResult.ShapeCollisionFunctionMask", N_ResultShapeCollisionFunctionMask },
            { "TraceResult.ShapeHierarchyId", N_ResultShapeHierarchyId },
            { "TraceResult.BodyTransform", N_ResultBodyTransform },
            { "CCSNavArea.Id", N_NavId },
            { "CCSNavArea.Center", N_NavVector<NavCenter> },
            { "CCSNavArea.Normal", N_NavVector<NavNormal> },
            { "CCSNavArea.Min", N_NavVector<NavMin> },
            { "CCSNavArea.Max", N_NavVector<NavMax> },
            { "CCSNavArea.Width", N_NavFloat<&CCSNavArea::Width> },
            { "CCSNavArea.Height", N_NavFloat<&CCSNavArea::Height> },
            { "CCSNavArea.Area2D", N_NavFloat<&CCSNavArea::Area2D> },
            { "CCSNavArea.GetHeightAtPosition", N_NavGetHeightAtPosition },
            { "CCSNavArea.ContainsPoint", N_NavContainsPoint },
            { "CCSNavArea.ContainsBox", N_NavContainsBox },
            { "CCSNavArea.IntersectsBox", N_NavIntersectsBox },
            { "CCSNavArea.GetClosestPoint", N_NavGetClosestPoint },
            { "CCSNavArea.GetDistanceToPoint", N_NavGetDistanceToPoint },
            { "CCSNavArea.GetDistanceSquaredToPoint", N_NavGetDistanceSquaredToPoint },
            { "CCSNavArea.GetAllNavAreas", N_NavGetAllNavAreas },
            { "CCSNavArea.GetClosestNavArea", N_NavGetClosestNavArea },
            { "CCSNavArea.IsValidNavMesh", N_NavIsValidNavMesh },

            { "Source2ModGameSystems.IsAvailable", N_GameSystemsIsAvailable },
            { "Source2ModGameSystems.FindByName", N_GameSystemsFindByName },

            { "Source2ModAddresses.IsResolved", N_AddressesIsResolved },
            { nullptr, nullptr },
        };
    }

    void* AddressOf(IPluginContext* ctx, cell_t address)
    {
        auto& a = addresses::source2modAddresses;
        cell_t index = 0;
#define S2M_ADDRESS(name)                                           \
        if (address == index++)                                     \
            return reinterpret_cast<void*>(a.name());
        SOURCE2MOD_SP_ADDRESSES(S2M_ADDRESS)
#undef S2M_ADDRESS
        ctx->ThrowNativeError("Invalid address %d", address);
        return nullptr;
    }

    void ForgetWorldHooks(PluginId id)
    {
        for (auto it = g_deniedHandlers.begin(); it != g_deniedHandlers.end();)
        {
            if (it->second == id)
            {
                permissions::permissionsManager.ClearAccessDeniedHandler(it->first);
                it = g_deniedHandlers.erase(it);
            }
            else
                ++it;
        }
    }

    cell_t NewTraceResult(IPluginContext* ctx, const CGameTrace* trace)
    {
        return NewResult(ctx, TraceResult(trace));
    }

    const sp_nativeinfo_t* GetWorldNatives()
    {
        return g_worldNatives;
    }
}
