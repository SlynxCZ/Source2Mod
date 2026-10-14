/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * Game hooks and transmit of the SourcePawn API mirroring the C++ one
 * (source2mod/gamehooks.inc, source2mod/transmit.inc), on the core's game
 * hooks and transmit managers.
 *
 * A handler's context (TakeDamageContext, PostThinkContext, the damage
 * info, the CheckTransmit viewer list) is a handle made for the call and
 * freed when the handler returns. The game hooks manager tells owners apart
 * by the handler's code, which for a script is the core's; the hooks a
 * script made are therefore remembered here and taken down when it unloads.
 *
 * Project: Source2Mod
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/gamehooks.h"
#include "core/transmit.h"

#include <source2mod/schema/takedamageinfo.h>
#include <source2mod/schema/entity/classes/CBaseEntity.h>
#include <source2mod/schema/entity/classes/CCSPlayerPawnBase.h>

#include <sp_callable.h>

#include <algorithm>
#include <map>
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

        cell_t IndexOf(CEntityInstance* inst)
        {
            return inst ? static_cast<cell_t>(inst->GetEntityIndex().Get()) : -1;
        }

        CEntityInstance* EntityAt(IPluginContext* ctx, cell_t index)
        {
            CEntityInstance* inst = (index >= 0 && index < 32768) ? CBaseEntity::FromIndex<CEntityInstance>(index) : nullptr;
            if (!inst)
                ctx->ThrowNativeError("Entity %d is invalid", index);
            return inst;
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

        HandleType TakeDamageType()
        {
            static const HandleType t = FrameType("TakeDamageContext");
            return t;
        }

        HandleType DamageInfoType()
        {
            static const HandleType t = FrameType("CTakeDamageInfo");
            return t;
        }

        HandleType PostThinkType()
        {
            static const HandleType t = FrameType("PostThinkContext");
            return t;
        }

        struct TakeDamageFrame
        {
            TakeDamageContext* ctx;
            cell_t info;
        };

        enum class HookKind
        {
            TakeDamage,
            PostThink,
        };

        struct ScriptGameHook
        {
            PluginId owner;
            HookKind kind;
        };
        std::map<GameHookId, ScriptGameHook> g_gameHooks;

        // HookTakeDamage(handler(TakeDamageContext ctx, bool post, int &result), bool post)
        cell_t N_HookTakeDamage(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[1]);
            if (!fn)
                return 0;
            const PluginId owner = CallerId(ctx);
            GameHookHandler<TakeDamageContext> handler([fn, owner](TakeDamageContext& c, bool post) -> GameHookReturn<std::int64_t>
            {
                TakeDamageFrame frame{ &c, Handles().Create(DamageInfoType(), c.info, owner) };
                const cell_t handle = Handles().Create(TakeDamageType(), &frame, owner);
                cell_t result = static_cast<cell_t>(c.result);

                sp::CallArgs args;
                args.PushCell(handle);
                args.PushCell(post ? 1 : 0);
                args.PushCellByRef(&result);
                cell_t res = 0;
                Invoke(fn, args, &res);

                Handles().Free(handle, 0, true);
                Handles().Free(frame.info, 0, true);
                const Action action = ToAction(res);
                if (action == Action::Ignore)
                    return action;
                return GameHookReturn<std::int64_t>(action, result);
            });
            const GameHookId id = gamehooks::gameHooksManager.HookTakeDamage(std::move(handler), params[2] != 0);
            if (id)
                g_gameHooks[id] = { owner, HookKind::TakeDamage };
            return id;
        }

        // HookPostThink(handler(PostThinkContext ctx, bool post), bool post)
        cell_t N_HookPostThink(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[1]);
            if (!fn)
                return 0;
            const PluginId owner = CallerId(ctx);
            GameHookHandler<PostThinkContext> handler([fn, owner](PostThinkContext& c, bool post) -> GameHookReturn<void>
            {
                const cell_t handle = Handles().Create(PostThinkType(), &c, owner);
                sp::CallArgs args;
                args.PushCell(handle);
                args.PushCell(post ? 1 : 0);
                cell_t res = 0;
                Invoke(fn, args, &res);
                Handles().Free(handle, 0, true);
                return ToAction(res);
            });
            const GameHookId id = gamehooks::gameHooksManager.HookPostThink(std::move(handler), params[2] != 0);
            if (id)
                g_gameHooks[id] = { owner, HookKind::PostThink };
            return id;
        }

        bool Unhook(GameHookId id)
        {
            auto it = g_gameHooks.find(id);
            if (it == g_gameHooks.end())
                return false;
            const HookKind kind = it->second.kind;
            g_gameHooks.erase(it);
            switch (kind)
            {
            case HookKind::TakeDamage:
                return gamehooks::gameHooksManager.UnhookTakeDamage(id);
            case HookKind::PostThink:
                return gamehooks::gameHooksManager.UnhookPostThink(id);
            }
            return false;
        }

        cell_t N_UnhookGameHook(IPluginContext* ctx, const cell_t* params)
        {
            auto it = g_gameHooks.find(params[1]);
            if (it == g_gameHooks.end() || it->second.owner != CallerId(ctx))
                return 0;
            return Unhook(params[1]);
        }

        TakeDamageFrame* DamageFrame(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<TakeDamageFrame*>(ReadHandle(ctx, handle, TakeDamageType()));
        }

        cell_t N_TDEntity(IPluginContext* ctx, const cell_t* params)
        {
            TakeDamageFrame* f = DamageFrame(ctx, params[1]);
            return f ? IndexOf(f->ctx->entity) : -1;
        }

        cell_t N_TDInfo(IPluginContext* ctx, const cell_t* params)
        {
            TakeDamageFrame* f = DamageFrame(ctx, params[1]);
            return f ? f->info : 0;
        }

        cell_t N_TDCallOriginal(IPluginContext* ctx, const cell_t* params)
        {
            TakeDamageFrame* f = DamageFrame(ctx, params[1]);
            return f ? static_cast<cell_t>(f->ctx->CallOriginal()) : 0;
        }

        cell_t N_PTPawn(IPluginContext* ctx, const cell_t* params)
        {
            auto* c = static_cast<PostThinkContext*>(ReadHandle(ctx, params[1], PostThinkType()));
            return c ? IndexOf(c->pawn) : -1;
        }

        cell_t N_PTCallOriginal(IPluginContext* ctx, const cell_t* params)
        {
            if (auto* c = static_cast<PostThinkContext*>(ReadHandle(ctx, params[1], PostThinkType())))
                c->CallOriginal();
            return 0;
        }

        // CTakeDamageInfo properties
        CTakeDamageInfo* Info(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<CTakeDamageInfo*>(ReadHandle(ctx, handle, DamageInfoType()));
        }

#define INFO_FLOAT(field)                                                                   \
        cell_t N_Info_get_##field(IPluginContext* ctx, const cell_t* params)                \
        {                                                                                   \
            CTakeDamageInfo* i = Info(ctx, params[1]);                                      \
            return i ? sp_ftoc(i->field) : 0;                                               \
        }                                                                                   \
        cell_t N_Info_set_##field(IPluginContext* ctx, const cell_t* params)                \
        {                                                                                   \
            if (CTakeDamageInfo* i = Info(ctx, params[1]))                                  \
                i->field = sp_ctof(params[2]);                                              \
            return 0;                                                                       \
        }
#define INFO_INT(field)                                                                     \
        cell_t N_Info_get_##field(IPluginContext* ctx, const cell_t* params)                \
        {                                                                                   \
            CTakeDamageInfo* i = Info(ctx, params[1]);                                      \
            return i ? static_cast<cell_t>(i->field) : 0;                                   \
        }                                                                                   \
        cell_t N_Info_set_##field(IPluginContext* ctx, const cell_t* params)                \
        {                                                                                   \
            if (CTakeDamageInfo* i = Info(ctx, params[1]))                                  \
                i->field = params[2];                                                       \
            return 0;                                                                       \
        }
#define INFO_HANDLE(field)                                                                  \
        cell_t N_Info_get_##field(IPluginContext* ctx, const cell_t* params)                \
        {                                                                                   \
            CTakeDamageInfo* i = Info(ctx, params[1]);                                      \
            return i ? IndexOf(i->field.Get()) : -1;                                        \
        }
#define INFO_VECTOR(field)                                                                  \
        cell_t N_Info_##field(IPluginContext* ctx, const cell_t* params)                    \
        {                                                                                   \
            if (CTakeDamageInfo* i = Info(ctx, params[1]))                                  \
            {                                                                               \
                cell_t* out;                                                                \
                ctx->LocalToPhysAddr(params[2], &out);                                      \
                out[0] = sp_ftoc(i->field.x);                                               \
                out[1] = sp_ftoc(i->field.y);                                               \
                out[2] = sp_ftoc(i->field.z);                                               \
            }                                                                               \
            return 0;                                                                       \
        }

        INFO_FLOAT(m_flDamage)
        INFO_FLOAT(m_flOriginalDamage)
        INFO_FLOAT(m_flTotalledDamage)
        INFO_INT(m_bitsDamageType)
        INFO_INT(m_iDamageCustom)
        INFO_HANDLE(m_hInflictor)
        INFO_HANDLE(m_hAttacker)
        INFO_HANDLE(m_hAbility)
        INFO_VECTOR(m_vecDamageForce)
        INFO_VECTOR(m_vecDamagePosition)
        INFO_VECTOR(m_vecDamageDirection)
#undef INFO_FLOAT
#undef INFO_INT
#undef INFO_HANDLE
#undef INFO_VECTOR

        cell_t N_Info_get_m_iHitGroupId(IPluginContext* ctx, const cell_t* params)
        {
            CTakeDamageInfo* i = Info(ctx, params[1]);
            return i ? static_cast<cell_t>(i->m_iHitGroupId) : 0;
        }

        cell_t N_Info_Set_m_vecDamageForce(IPluginContext* ctx, const cell_t* params)
        {
            if (CTakeDamageInfo* i = Info(ctx, params[1]))
            {
                cell_t* in;
                ctx->LocalToPhysAddr(params[2], &in);
                i->m_vecDamageForce = Vector(sp_ctof(in[0]), sp_ctof(in[1]), sp_ctof(in[2]));
            }
            return 0;
        }

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
            return e ? IndexOf(TM().GetOwner(e)) : -1;
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
            return static_cast<ISource2ModTransmit&>(TM()).UnhookCheckTransmit(static_cast<Source2ModHookId>(params[1]));
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

        const sp_nativeinfo_t g_gameHookNatives[] = {
            // source2mod/gamehooks.inc
            { "Source2ModGameHooks.HookTakeDamage", N_HookTakeDamage },
            { "Source2ModGameHooks.UnhookTakeDamage", N_UnhookGameHook },
            { "Source2ModGameHooks.HookPostThink", N_HookPostThink },
            { "Source2ModGameHooks.UnhookPostThink", N_UnhookGameHook },
            { "TakeDamageContext.entity.get", N_TDEntity },
            { "TakeDamageContext.info.get", N_TDInfo },
            { "TakeDamageContext.CallOriginal", N_TDCallOriginal },
            { "PostThinkContext.pawn.get", N_PTPawn },
            { "PostThinkContext.CallOriginal", N_PTCallOriginal },
            { "CTakeDamageInfo.m_flDamage.get", N_Info_get_m_flDamage },
            { "CTakeDamageInfo.m_flDamage.set", N_Info_set_m_flDamage },
            { "CTakeDamageInfo.m_flOriginalDamage.get", N_Info_get_m_flOriginalDamage },
            { "CTakeDamageInfo.m_flTotalledDamage.get", N_Info_get_m_flTotalledDamage },
            { "CTakeDamageInfo.m_bitsDamageType.get", N_Info_get_m_bitsDamageType },
            { "CTakeDamageInfo.m_bitsDamageType.set", N_Info_set_m_bitsDamageType },
            { "CTakeDamageInfo.m_iDamageCustom.get", N_Info_get_m_iDamageCustom },
            { "CTakeDamageInfo.m_iDamageCustom.set", N_Info_set_m_iDamageCustom },
            { "CTakeDamageInfo.m_iHitGroupId.get", N_Info_get_m_iHitGroupId },
            { "CTakeDamageInfo.m_hInflictor.get", N_Info_get_m_hInflictor },
            { "CTakeDamageInfo.m_hAttacker.get", N_Info_get_m_hAttacker },
            { "CTakeDamageInfo.m_hAbility.get", N_Info_get_m_hAbility },
            { "CTakeDamageInfo.m_vecDamageForce", N_Info_m_vecDamageForce },
            { "CTakeDamageInfo.Set_m_vecDamageForce", N_Info_Set_m_vecDamageForce },
            { "CTakeDamageInfo.m_vecDamagePosition", N_Info_m_vecDamagePosition },
            { "CTakeDamageInfo.m_vecDamageDirection", N_Info_m_vecDamageDirection },
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
            { nullptr, nullptr },
        };
    }

    void ForgetGameHooks(PluginId id)
    {
        std::vector<GameHookId> ids;
        for (const auto& [hookId, hook] : g_gameHooks)
            if (hook.owner == id)
                ids.push_back(hookId);
        for (GameHookId hookId : ids)
            Unhook(hookId);
    }

    const sp_nativeinfo_t* GetGameHookNatives()
    {
        return g_gameHookNatives;
    }
}
