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
#include "gamehooks.h"

#include "hookid.h"
#include "hooks.h"

#include <algorithm>
#include <tuple>
#include <type_traits>

// KHook, via metamod.
#include "ISmmPlugin.h"

#include "dynlibutils/module.hpp"
#include "source2mod/ISource2ModKHook.h"
#include "shared.h"
#include "tkvprof.h"
#include "utils/log.h"
#include "slowguard.h"
#include "pluginmanager.h"

#include "source2mod/schema/takedamageinfo.h"
#include "source2mod/schema/entity/classes/CBaseEntity.h"
#include "source2mod/schema/entity/classes/CBasePlayerController.h"
#include "source2mod/schema/entity/classes/CCSPlayerController.h"
#include "source2mod/schema/entity/classes/CCSPlayerPawnBase.h"
#include <entity2/entityidentity.h>
#include <tier0/utlsymbollarge.h>

#include <cstring>
#include <string>

// The hooked functions take and return pointers to these; nothing here
// looks inside them, so the forward declarations of the SDK header do.

namespace gamehooks
{
    GameHooksManager gameHooksManager;

    namespace
    {
        // What a call is about, for the log; the damage hook says who and how much.
        template <typename CONTEXT>
        std::string Describe(const CONTEXT&)
        {
            return {};
        }

        std::string Describe(const TakeDamageContext& ctx)
        {
            std::string s = "victim=";
            s += ctx.entity ? ctx.entity->GetClassname() : "null";
            if (ctx.info)
                s += " damage=" + std::to_string(ctx.info->m_flDamage) + " bits=" + std::to_string(ctx.info->m_bitsDamageType);
            return s;
        }

        template <typename RETURN>
        std::string ValueText(const RETURN& value)
        {
            if constexpr (std::is_arithmetic_v<RETURN> || std::is_enum_v<RETURN>)
                return std::to_string(static_cast<long long>(value));
            else
                return "-";
        }

        using hooks::DebugOn;
        using hooks::ActionName;

        template <typename CONTEXT>
        using Listeners = hooks::PointList<CONTEXT>;

        // What a call is about for the targets and filters: the hooked object,
        // the entity whose designer name a filter compares, the input name.
        hooks::Subject SubjectOf(const TakeDamageContext& c) { return { c.entity, c.entity, nullptr }; }
        hooks::Subject SubjectOf(const CanAcquireContext& c) { return { c.services, nullptr, nullptr }; }
        hooks::Subject SubjectOf(const CanMoveContext& c) { return { c.pawn, c.pawn, nullptr }; }
        hooks::Subject SubjectOf(const CanUseContext& c) { return { c.services, nullptr, nullptr }; }
        hooks::Subject SubjectOf(const PostThinkContext& c) { return { c.pawn, c.pawn, nullptr }; }
        hooks::Subject SubjectOf(const ProcessUsercmdsContext& c) { return { c.controller, c.controller, nullptr }; }
        hooks::Subject SubjectOf(const SimulateUserCommandsContext& c) { return { c.controller, c.controller, nullptr }; }
        hooks::Subject SubjectOf(const RunCommandContext& c) { return { c.services, nullptr, nullptr }; }
        hooks::Subject SubjectOf(const AcceptInputContext& c)
        {
            CEntityInstance* entity = c.identity ? c.identity->m_pInstance : nullptr;
            return { entity, entity, c.inputName ? c.inputName->String() : nullptr };
        }
        hooks::Subject SubjectOf(const TouchContext& c) { return { c.entity, c.entity, nullptr }; }
        hooks::Subject SubjectOf(const DropWeaponContext& c) { return { c.services, nullptr, nullptr }; }
        hooks::Subject SubjectOf(const MovementContext& c) { return { c.services, nullptr, nullptr }; }
        hooks::Subject SubjectOf(const AirAccelerateContext& c) { return { c.services, nullptr, nullptr }; }
        hooks::Subject SubjectOf(const GroundAccelerateContext& c) { return { c.services, nullptr, nullptr }; }
        hooks::Subject SubjectOf(const CategorizePositionContext& c) { return { c.services, nullptr, nullptr }; }
        hooks::Subject SubjectOf(const CheckVelocityContext& c) { return { c.services, nullptr, nullptr }; }
        hooks::Subject SubjectOf(const FullWalkMoveContext& c) { return { c.services, nullptr, nullptr }; }
        hooks::Subject SubjectOf(const SetupMoveContext& c) { return { c.services, nullptr, nullptr }; }
        hooks::Subject SubjectOf(const TryPlayerMoveContext& c) { return { c.services, nullptr, nullptr }; }
        hooks::Subject SubjectOf(const LegacyJumpContext& c) { return { c.jump, nullptr, nullptr }; }
        hooks::Subject SubjectOf(const ModernJumpContext& c) { return { c.jump, nullptr, nullptr }; }

        // The context member the return value lives in; nothing for void.
        template <typename CONTEXT, typename RETURN>
        struct ResultMemberOf
        {
            using type = RETURN CONTEXT::*;
        };

        template <typename CONTEXT>
        struct ResultMemberOf<CONTEXT, void>
        {
            using type = std::nullptr_t;
        };

        // What every kind shares: listeners, "install on the first listener"
        // (kept until shutdown, see gamehooks.h), and the pre/post dispatch
        // that builds the context and applies its result.
        template <typename CONTEXT, typename RETURN, typename CLASS, typename... ARGS>
        class GameHookBase : public IGameHook
        {
        public:
            using MakeContext = CONTEXT (*)(CLASS*, ARGS...);
            using ResultMember = typename ResultMemberOf<CONTEXT, RETURN>::type;

            GameHookBase(const char* pszName, MakeContext make, ResultMember result)
                : m_pszName(pszName), m_make(make), m_result(result)
            {
            }

            const char* Name() const override { return m_pszName; }

            Source2ModHookId Add(PluginId owner, const void* handler, bool post, const HookTarget& target, const HookFilter& filter) override
            {
                const Source2ModHookId id = m_listeners.Add(owner, *static_cast<const HookHandler<CONTEXT>*>(handler), post, target, filter);
                Install();
                return id;
            }

            PluginId OwnerOf(const void* handler) const override
            {
                return hookid::OwnerOfHandler(*static_cast<const HookHandler<CONTEXT>*>(handler));
            }

            bool Remove(Source2ModHookId id) override { return m_listeners.Remove(id); }

            void RemoveAll(PluginId owner) override { m_listeners.RemoveOwner(owner); }

            void RemoveInstance(const void* instance) override { m_listeners.RemoveInstance(instance); }

            bool Empty() const override { return m_listeners.Empty(); }

            bool IsAvailable() override { return Resolve(); }

            KHook::Return<RETURN> Pre(CLASS* pThis, ARGS... args)
            {
                if (!m_listeners.Has(false))
                    return Ignore();

                CONTEXT ctx = m_make(pThis, args...);
                const CallData call{ this, pThis, std::tuple<ARGS...>(args...) };
                BindCallOriginal(ctx, call);
                const Action action = m_listeners.Dispatch(ctx, false, SubjectOf(ctx), m_pszName, ApplyValue());

                if (DebugOn(m_pszName))
                {
                    if constexpr (std::is_void_v<RETURN>)
                        FP_INFO("hookdebug {} pre: {} {}", m_pszName, ActionName(action), Describe(ctx));
                    else
                        FP_INFO("hookdebug {} pre: {} (return {}) {}", m_pszName, ActionName(action),
                                action == Action::Ignore ? std::string("original") : ValueText(ctx.*m_result), Describe(ctx));
                }

                if constexpr (std::is_void_v<RETURN>)
                {
                    // Nothing to override without a return value; Supersede still skips the original.
                    return { action == Action::Supersede ? KHook::Action::Supersede : KHook::Action::Ignore };
                }
                else
                {
                    if (action == Action::Ignore)
                        return Ignore();

                    return { action, ctx.*m_result };
                }
            }

            KHook::Return<RETURN> Post(CLASS* pThis, ARGS... args)
            {
                if (!m_listeners.Has(true))
                    return Ignore();

                CONTEXT ctx = m_make(pThis, args...);
                const CallData call{ this, pThis, std::tuple<ARGS...>(args...) };
                BindCallOriginal(ctx, call);

                if constexpr (!std::is_void_v<RETURN>)
                {
                    if (auto* original = static_cast<RETURN*>(KHook::GetOriginalValuePtr()))
                        ctx.*m_result = *original;
                }

                const Action action = m_listeners.Dispatch(ctx, true, SubjectOf(ctx), m_pszName, ApplyValue());

                if (DebugOn(m_pszName))
                {
                    if constexpr (std::is_void_v<RETURN>)
                        FP_INFO("hookdebug {} post: {} {}", m_pszName, ActionName(action), Describe(ctx));
                    else
                        FP_INFO("hookdebug {} post: {} (return {}) {}", m_pszName, ActionName(action), ValueText(ctx.*m_result), Describe(ctx));
                }

                if constexpr (std::is_void_v<RETURN>)
                {
                    return { KHook::Action::Ignore };
                }
                else
                {
                    if (action == Action::Ignore)
                        return Ignore();

                    return { KHook::Action::Override, ctx.*m_result };
                }
            }

        protected:
            // The game's function past every hook on it (KHook's CallOriginal);
            // the context's CallOriginal() lands here.
            virtual RETURN CallOriginalRaw(CLASS* pThis, ARGS... args) = 0;

            // What a context's CallOriginal() needs: the arguments the game
            // passed. Lives on the stack of Pre()/Post() for the dispatch.
            struct CallData
            {
                GameHookBase* hook;
                CLASS* pThis;
                std::tuple<ARGS...> args;
            };

            using ContextReturn = decltype(std::declval<const CONTEXT&>().CallOriginal());

            static ContextReturn CallOriginalThunk(const void* data)
            {
                const auto* call = static_cast<const CallData*>(data);
                if constexpr (std::is_void_v<RETURN>)
                {
                    std::apply([call](ARGS... a) { call->hook->CallOriginalRaw(call->pThis, a...); }, call->args);
                    // A context shared with bool functions answers false for a void one.
                    if constexpr (!std::is_void_v<ContextReturn>)
                        return ContextReturn{};
                }
                else
                {
                    return static_cast<ContextReturn>(
                        std::apply([call](ARGS... a) { return call->hook->CallOriginalRaw(call->pThis, a...); }, call->args));
                }
            }

            // Where a returned value goes: ctx.result, as the function's type.
            // A context shared with void functions has nowhere to put it.
            auto ApplyValue() const
            {
                return [this](CONTEXT& ctx, const auto& value)
                {
                    if constexpr (!std::is_void_v<RETURN>)
                        ctx.*m_result = static_cast<RETURN>(value);
                };
            }

            static void BindCallOriginal(CONTEXT& ctx, const CallData& call)
            {
                ctx.callOriginal_ = &CallOriginalThunk;
                ctx.callOriginalData_ = &call;
            }

            static KHook::Return<RETURN> Ignore()
            {
                if constexpr (std::is_void_v<RETURN>)
                    return { KHook::Action::Ignore };
                else
                    return { KHook::Action::Ignore, RETURN{} };
            }

            // Finds the function; false, said once, when gamedata has nothing.
            virtual bool Resolve() = 0;
            // Places the detour when it is not in yet and the function is known.
            virtual void Install() = 0;

            const char* m_pszName;
            MakeContext m_make;
            ResultMember m_result;
            Listeners<CONTEXT> m_listeners;
            bool m_bWarned = false;
        };

        // A function found by signature, detoured by address.
        template <typename CONTEXT, typename RETURN, typename CLASS, typename... ARGS>
        class MemberGameHook final : public GameHookBase<CONTEXT, RETURN, CLASS, ARGS...>
        {
            using Base = GameHookBase<CONTEXT, RETURN, CLASS, ARGS...>;
            using Self = MemberGameHook;

        public:
            using Base::Base;

            ~MemberGameHook() override { Uninstall(); }

            void Uninstall() override
            {
                // Synchronous: only from Shutdown(), never from a callback of
                // this hook.
                delete m_pHook;
                m_pHook = nullptr;
            }

        protected:
            bool Resolve() override
            {
                if (m_address)
                    return true;

                const ISource2ModMemory address = shared::g_pGameConfig ? shared::g_pGameConfig->ResolveSignature(this->m_pszName) : ISource2ModMemory();
                if (!address)
                {
                    if (!this->m_bWarned)
                    {
                        this->m_bWarned = true;
                        FP_WARN("Game hook '{}': no signature in gamedata for this platform; its handlers never run", this->m_pszName);
                    }
                    return false;
                }

                m_address = address.GetPtr();
                return true;
            }

            void Install() override
            {
                if (m_pHook || !Resolve())
                    return;

                // Pre/Post are the base's, so the context is the base too.
                m_pHook = new KHook::Member<CLASS, RETURN, ARGS...>(static_cast<Base*>(this), &Base::Pre, &Base::Post);
                m_pHook->Configure(m_address);
            }

            RETURN CallOriginalRaw(CLASS* pThis, ARGS... args) override
            {
                return m_pHook->CallOriginal(pThis, args...);
            }

        private:
            void* m_address = nullptr;
            KHook::Member<CLASS, RETURN, ARGS...>* m_pHook = nullptr;
        };

        // A virtual function: gamedata offset on the vtable of an RTTI class in
        // libserver, hooked for every object sharing that vtable.
        template <typename CONTEXT, typename RETURN, typename CLASS, typename... ARGS>
        class VirtualGameHook final : public GameHookBase<CONTEXT, RETURN, CLASS, ARGS...>
        {
            using Base = GameHookBase<CONTEXT, RETURN, CLASS, ARGS...>;
            using Self = VirtualGameHook;

        public:
            VirtualGameHook(const char* pszName, const char* pszVTable, typename Base::MakeContext make, typename Base::ResultMember result)
                : Base(pszName, make, result), m_pszVTable(pszVTable)
            {
            }

            ~VirtualGameHook() override { Uninstall(); }

            void Uninstall() override
            {
                if (!m_pHook)
                    return;

                m_pHook->RemoveGlobal(reinterpret_cast<CLASS*>(&m_pVTable));
                delete m_pHook;
                m_pHook = nullptr;
            }

        protected:
            bool Resolve() override
            {
                if (m_index >= 0 && m_pVTable)
                    return true;

                m_index = shared::g_pGameConfig ? shared::g_pGameConfig->GetOffset(this->m_pszName) : -1;

                if (m_index >= 0)
                {
                    DynLibUtils::CModule server(g_pSource2Server);
                    m_pVTable = server.GetVirtualTableByName(m_pszVTable).GetPtr();
                }

                if (m_index < 0 || !m_pVTable)
                {
                    if (!this->m_bWarned)
                    {
                        this->m_bWarned = true;
                        FP_WARN("Game hook '{}': {} for this platform; its handlers never run", this->m_pszName,
                                m_index < 0 ? "no offset in gamedata" : "vtable not found");
                    }
                    return false;
                }

                return true;
            }

            void Install() override
            {
                if (m_pHook || !Resolve())
                    return;

                m_pHook = new Source2ModKHook::Virtual<CLASS, RETURN, ARGS...>(static_cast<std::uint32_t>(m_index), static_cast<Base*>(this), &Base::Pre, &Base::Post);
                // AddGlobal reads the first pointer of what it is handed, so a
                // pointer holding the vtable stands in for an object.
                m_pHook->AddGlobal(reinterpret_cast<CLASS*>(&m_pVTable));
            }

            RETURN CallOriginalRaw(CLASS* pThis, ARGS... args) override
            {
                return m_pHook->CallOriginal(pThis, args...);
            }

        private:
            const char* m_pszVTable;
            int m_index = -1;
            void* m_pVTable = nullptr;
            Source2ModKHook::Virtual<CLASS, RETURN, ARGS...>* m_pHook = nullptr;
        };

        template <typename HOOK>
        HOOK& As(std::unique_ptr<IGameHook>& p)
        {
            return *static_cast<HOOK*>(p.get());
        }

        // The movement functions that take the move data alone share a context.
        using MoveServices = CCSPlayer_MovementServices;
        MovementContext MakeMovement(MoveServices* s, CMoveData* mv) { return MovementContext{ s, mv, false }; }
    }

    // ---- the table: prototypes as ISource2ModAddresses.h has them -------------

    using TakeDamageHook = MemberGameHook<TakeDamageContext, std::int64_t, CBaseEntity, CTakeDamageInfo*, CTakeDamageResult*>;
    using CanAcquireHook = MemberGameHook<CanAcquireContext, AcquireResult, CCSPlayer_ItemServices, CEconItemView*, AcquireMethod, void*>;
    using CanMoveHook = MemberGameHook<CanMoveContext, bool, CCSPlayerPawnBase>;
    using CanUseHook = VirtualGameHook<CanUseContext, bool, CCSPlayer_WeaponServices, CBasePlayerWeapon*>;
    using PostThinkHook = MemberGameHook<PostThinkContext, void, CCSPlayerPawnBase>;
    using ProcessUsercmdsHook = MemberGameHook<ProcessUsercmdsContext, void*, CCSPlayerController, void*, int, bool, float>;
    using SimulateUserCommandsHook = MemberGameHook<SimulateUserCommandsContext, void, CBasePlayerController>;
    using RunCommandHook = VirtualGameHook<RunCommandContext, void, MoveServices, void*>;
    using AcceptInputHook = MemberGameHook<AcceptInputContext, bool, CEntityIdentity, CUtlSymbolLarge*, CEntityInstance*, CEntityInstance*, variant_t*, void*, void*>;
    using TouchHook = VirtualGameHook<TouchContext, void, CBaseEntity, CBaseEntity*>;
    using DropWeaponHook = VirtualGameHook<DropWeaponContext, void, CCSPlayer_WeaponServices, CBasePlayerWeapon*, Vector*, Vector*>;
    using MoveVoidHook = MemberGameHook<MovementContext, void, MoveServices, CMoveData*>;
    using MoveBoolHook = MemberGameHook<MovementContext, bool, MoveServices, CMoveData*>;
    using AirAccelerateHook = MemberGameHook<AirAccelerateContext, void, MoveServices, CMoveData*, Vector*, float, float>;
#ifdef _WIN32
    // The wish direction and the frame time swap places between platforms;
    // the context has one order.
    using GroundAccelerateHook = MemberGameHook<GroundAccelerateContext, void, MoveServices, CMoveData*, float, Vector*, float, float>;
#else
    using GroundAccelerateHook = MemberGameHook<GroundAccelerateContext, void, MoveServices, CMoveData*, Vector*, float, float, float>;
#endif
    using CategorizePositionHook = MemberGameHook<CategorizePositionContext, void, MoveServices, CMoveData*, bool>;
    using CheckVelocityHook = MemberGameHook<CheckVelocityContext, void, MoveServices, CMoveData*, void*>;
    using FullWalkMoveHook = MemberGameHook<FullWalkMoveContext, void, MoveServices, CMoveData*, bool>;
    using SetupMoveHook = MemberGameHook<SetupMoveContext, void, MoveServices, CUserCmd*, CMoveData*>;
    using TryPlayerMoveHook = MemberGameHook<TryPlayerMoveContext, void, MoveServices, CMoveData*, Vector*, CGameTrace*, bool*>;
    using LegacyJumpHook = MemberGameHook<LegacyJumpContext, void, CCSPlayerLegacyJump, CMoveData*>;
    using ModernJumpHook = MemberGameHook<ModernJumpContext, void, CCSPlayerModernJump, CMoveData*>;

    GameHooksManager::GameHooksManager()
    {
        m_hooks.resize(kGameHookCount);
        auto slot = [this](HookPointId h, IGameHook* hook) { m_hooks[static_cast<size_t>(h)].reset(hook); };

        slot(HookPointId::TakeDamage, new TakeDamageHook("CBaseEntity::TakeDamageOld",
            [](CBaseEntity* e, CTakeDamageInfo* info, CTakeDamageResult* result) { return TakeDamageContext{ e, info, result, 0 }; },
            &TakeDamageContext::result));
        slot(HookPointId::CanAcquire, new CanAcquireHook("CCSPlayer_ItemServices::CanAcquire",
            [](CCSPlayer_ItemServices* s, CEconItemView* item, AcquireMethod method, void* unk) { return CanAcquireContext{ s, item, method, unk, AcquireResult::Allowed }; },
            &CanAcquireContext::result));
        slot(HookPointId::CanMove, new CanMoveHook("CCSPlayerPawnBase::CanMove",
            [](CCSPlayerPawnBase* pawn) { return CanMoveContext{ pawn, false }; }, &CanMoveContext::result));
        slot(HookPointId::CanUse, new CanUseHook("CCSPlayer_WeaponServices::CanUse", "CCSPlayer_WeaponServices",
            [](CCSPlayer_WeaponServices* s, CBasePlayerWeapon* w) { return CanUseContext{ s, w, false }; }, &CanUseContext::result));
        slot(HookPointId::PostThink, new PostThinkHook("CCSPlayerPawn::PostThink",
            [](CCSPlayerPawnBase* pawn) { return PostThinkContext{ pawn }; }, nullptr));
        slot(HookPointId::ProcessUsercmds, new ProcessUsercmdsHook("CCSPlayerController::ProcessUserCmd",
            [](CCSPlayerController* c, void* cmds, int count, bool paused, float margin) { return ProcessUsercmdsContext{ c, cmds, count, paused, margin, nullptr }; },
            &ProcessUsercmdsContext::result));
        slot(HookPointId::SimulateUserCommands, new SimulateUserCommandsHook("CBasePlayerController::OnSimulateUserCommands",
            [](CBasePlayerController* c) { return SimulateUserCommandsContext{ c }; }, nullptr));
        slot(HookPointId::RunCommand, new RunCommandHook("CPlayer_MovementServices::RunCommand", "CCSPlayer_MovementServices",
            [](MoveServices* s, void* cmd) { return RunCommandContext{ s, cmd }; }, nullptr));
        slot(HookPointId::AcceptInput, new AcceptInputHook("CEntityIdentity::AcceptInput",
            [](CEntityIdentity* i, CUtlSymbolLarge* name, CEntityInstance* activator, CEntityInstance* caller, variant_t* value, void*, void*) { return AcceptInputContext{ i, name, activator, caller, value, false }; },
            &AcceptInputContext::result));
        slot(HookPointId::Touch, new TouchHook("CBaseEntity::Touch", "CBaseEntity",
            [](CBaseEntity* e, CBaseEntity* other) { return TouchContext{ e, other }; }, nullptr));
        slot(HookPointId::DropWeapon, new DropWeaponHook("CCSPlayer_WeaponServices::DropWeapon", "CCSPlayer_WeaponServices",
            [](CCSPlayer_WeaponServices* s, CBasePlayerWeapon* w, Vector* target, Vector* velocity) { return DropWeaponContext{ s, w, target, velocity }; }, nullptr));
        slot(HookPointId::AirAccelerate, new AirAccelerateHook("CCSPlayer_MovementServices::AirAccelerate",
            [](MoveServices* s, CMoveData* mv, Vector* dir, float speed, float accel) { return AirAccelerateContext{ s, mv, dir, speed, accel }; }, nullptr));
        slot(HookPointId::AirMove, new MoveVoidHook("CCSPlayer_MovementServices::AirMove", &MakeMovement, nullptr));
        slot(HookPointId::CanUnduck, new MoveBoolHook("CCSPlayer_MovementServices::CanUnduck", &MakeMovement, &MovementContext::result));
        slot(HookPointId::CategorizePosition, new CategorizePositionHook("CCSPlayer_MovementServices::CategorizePosition",
            [](MoveServices* s, CMoveData* mv, bool stayOnGround) { return CategorizePositionContext{ s, mv, stayOnGround }; }, nullptr));
        slot(HookPointId::CheckFalling, new MoveVoidHook("CCSPlayer_MovementServices::CheckFalling", &MakeMovement, nullptr));
        slot(HookPointId::CheckParameters, new MoveVoidHook("CCSPlayer_MovementServices::CheckParameters", &MakeMovement, nullptr));
        slot(HookPointId::CheckVelocity, new CheckVelocityHook("CCSPlayer_MovementServices::CheckVelocity",
            [](MoveServices* s, CMoveData* mv, void* unk) { return CheckVelocityContext{ s, mv, unk }; }, nullptr));
        slot(HookPointId::CheckWater, new MoveBoolHook("CCSPlayer_MovementServices::CheckWater", &MakeMovement, &MovementContext::result));
        slot(HookPointId::Duck, new MoveVoidHook("CCSPlayer_MovementServices::Duck", &MakeMovement, nullptr));
        slot(HookPointId::Friction, new MoveVoidHook("CCSPlayer_MovementServices::Friction", &MakeMovement, nullptr));
        slot(HookPointId::FullWalkMove, new FullWalkMoveHook("CCSPlayer_MovementServices::FullWalkMove",
            [](MoveServices* s, CMoveData* mv, bool onGround) { return FullWalkMoveContext{ s, mv, onGround }; }, nullptr));
#ifdef _WIN32
        slot(HookPointId::GroundAccelerate, new GroundAccelerateHook("CCSPlayer_MovementServices::GroundAccelerate",
            [](MoveServices* s, CMoveData* mv, float frameTime, Vector* dir, float speed, float accel) { return GroundAccelerateContext{ s, mv, dir, frameTime, speed, accel }; }, nullptr));
#else
        slot(HookPointId::GroundAccelerate, new GroundAccelerateHook("CCSPlayer_MovementServices::GroundAccelerate",
            [](MoveServices* s, CMoveData* mv, Vector* dir, float frameTime, float speed, float accel) { return GroundAccelerateContext{ s, mv, dir, frameTime, speed, accel }; }, nullptr));
#endif
        slot(HookPointId::LadderMove, new MoveBoolHook("CCSPlayer_MovementServices::LadderMove", &MakeMovement, &MovementContext::result));
        slot(HookPointId::MoveInit, new MoveBoolHook("CCSPlayer_MovementServices::MoveInit", &MakeMovement, &MovementContext::result));
        slot(HookPointId::PlayerMove, new MoveVoidHook("CCSPlayer_MovementServices::PlayerMove", &MakeMovement, nullptr));
        slot(HookPointId::ProcessMovement, new MoveVoidHook("CCSPlayer_MovementServices::ProcessMovement", &MakeMovement, nullptr));
        slot(HookPointId::SetupMove, new SetupMoveHook("CCSPlayer_MovementServices::SetupMove",
            [](MoveServices* s, CUserCmd* cmd, CMoveData* mv) { return SetupMoveContext{ s, cmd, mv }; }, nullptr));
        slot(HookPointId::TryPlayerMove, new TryPlayerMoveHook("CCSPlayer_MovementServices::TryPlayerMove",
            [](MoveServices* s, CMoveData* mv, Vector* dest, CGameTrace* trace, bool* surfing) { return TryPlayerMoveContext{ s, mv, dest, trace, surfing }; }, nullptr));
        slot(HookPointId::WalkMove, new MoveVoidHook("CCSPlayer_MovementServices::WalkMove", &MakeMovement, nullptr));
        slot(HookPointId::WaterMove, new MoveVoidHook("CCSPlayer_MovementServices::WaterMove", &MakeMovement, nullptr));
        slot(HookPointId::OnJumpLegacy, new LegacyJumpHook("CCSPlayerLegacyJump::OnJump",
            [](CCSPlayerLegacyJump* j, CMoveData* mv) { return LegacyJumpContext{ j, mv }; }, nullptr));
        slot(HookPointId::OnJumpModern, new ModernJumpHook("CCSPlayerModernJump::OnJump",
            [](CCSPlayerModernJump* j, CMoveData* mv) { return ModernJumpContext{ j, mv }; }, nullptr));
        slot(HookPointId::CheckJumpButtonLegacy, new LegacyJumpHook("CCSPlayerLegacyJump::CheckJumpButton",
            [](CCSPlayerLegacyJump* j, CMoveData* mv) { return LegacyJumpContext{ j, mv }; }, nullptr));
        slot(HookPointId::CheckJumpButtonModern, new ModernJumpHook("CCSPlayerModernJump::CheckJumpButton",
            [](CCSPlayerModernJump* j, CMoveData* mv) { return ModernJumpContext{ j, mv }; }, nullptr));
    }

    GameHooksManager::~GameHooksManager() = default;

    void GameHooksManager::Shutdown()
    {
        for (auto& hook : m_hooks)
            hook->Uninstall();
    }

    void GameHooksManager::RemoveAllForPlugin(PluginId id)
    {
        // Only the handlers go; the detour stays (gamehooks.h).
        for (auto& hook : m_hooks)
            hook->RemoveAll(id);
    }

    namespace
    {
        bool InTable(HookPointId point)
        {
            return static_cast<int>(point) >= 0 && static_cast<size_t>(point) < kGameHookCount;
        }
    }

    Source2ModHookId GameHooksManager::Add(HookPointId point, PluginId owner, const void* handler, bool post,
                                           const HookTarget& target, const HookFilter& filter)
    {
        if (!InTable(point))
            return 0;
        return m_hooks[static_cast<size_t>(point)]->Add(owner, handler, post, target, filter);
    }

    PluginId GameHooksManager::OwnerOfHandler(HookPointId point, const void* handler) const
    {
        return InTable(point) ? m_hooks[static_cast<size_t>(point)]->OwnerOf(handler) : 0;
    }

    // The detour stays (gamehooks.h); an Unhook() may come from inside the
    // hook's own dispatch, which iterates a copy of the list.
    bool GameHooksManager::Remove(Source2ModHookId id)
    {
        for (auto& hook : m_hooks)
            if (hook->Remove(id))
                return true;
        return false;
    }

    void GameHooksManager::RemoveInstance(const void* instance)
    {
        for (auto& hook : m_hooks)
            hook->RemoveInstance(instance);
    }

    bool GameHooksManager::IsAvailable(HookPointId point)
    {
        return InTable(point) && m_hooks[static_cast<size_t>(point)]->IsAvailable();
    }
}
