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
#pragma once

#include <source2mod/ISource2ModGameHooks.h>

#include <memory>
#include <vector>

namespace gamehooks
{
    // One hooked game function: its listeners and, while it has any, its
    // detour. The concrete kinds are in gamehooks.cpp.
    class IGameHook
    {
    public:
        virtual ~IGameHook() = default;

        virtual const char* Name() const = 0;
        virtual bool IsAvailable() = 0;
        virtual bool Remove(Source2ModGameHookId id) = 0;
        // `handler` is the GameHookHandler<> of this hook's context.
        virtual bool RemoveHandler(PluginId owner, const void* handler, bool post) = 0;
        virtual void RemoveAll(PluginId owner) = 0;
        virtual bool Empty() const = 0;
        virtual void Uninstall() = 0;
    };

    class GameHooksManager final : public ISource2ModGameHooks
    {
    public:
        GameHooksManager();
        ~GameHooksManager() override;

        /// Takes every detour out, synchronously. Called from Unload(), where
        /// none of the hooked functions is on the stack.
        void Shutdown();

        /// Once a frame: a hook whose last listener left is taken out here,
        /// never from inside its own dispatch.
        void Tick();

        void RemoveAllForPlugin(PluginId id);

        Source2ModGameHookId HookTakeDamage(GameHookHandler<TakeDamageContext> handler, bool post) override;
        Source2ModGameHookId HookCanAcquire(GameHookHandler<CanAcquireContext> handler, bool post) override;
        Source2ModGameHookId HookCanMove(GameHookHandler<CanMoveContext> handler, bool post) override;
        Source2ModGameHookId HookCanUse(GameHookHandler<CanUseContext> handler, bool post) override;
        Source2ModGameHookId HookPostThink(GameHookHandler<PostThinkContext> handler, bool post) override;
        Source2ModGameHookId HookProcessUsercmds(GameHookHandler<ProcessUsercmdsContext> handler, bool post) override;
        Source2ModGameHookId HookSimulateUserCommands(GameHookHandler<SimulateUserCommandsContext> handler, bool post) override;
        Source2ModGameHookId HookRunCommand(GameHookHandler<RunCommandContext> handler, bool post) override;
        Source2ModGameHookId HookAcceptInput(GameHookHandler<AcceptInputContext> handler, bool post) override;
        Source2ModGameHookId HookTouch(GameHookHandler<TouchContext> handler, bool post) override;
        Source2ModGameHookId HookDropWeapon(GameHookHandler<DropWeaponContext> handler, bool post) override;
        Source2ModGameHookId HookAirAccelerate(GameHookHandler<AirAccelerateContext> handler, bool post) override;
        Source2ModGameHookId HookAirMove(GameHookHandler<MovementContext> handler, bool post) override;
        Source2ModGameHookId HookCanUnduck(GameHookHandler<MovementContext> handler, bool post) override;
        Source2ModGameHookId HookCategorizePosition(GameHookHandler<CategorizePositionContext> handler, bool post) override;
        Source2ModGameHookId HookCheckFalling(GameHookHandler<MovementContext> handler, bool post) override;
        Source2ModGameHookId HookCheckParameters(GameHookHandler<MovementContext> handler, bool post) override;
        Source2ModGameHookId HookCheckVelocity(GameHookHandler<CheckVelocityContext> handler, bool post) override;
        Source2ModGameHookId HookCheckWater(GameHookHandler<MovementContext> handler, bool post) override;
        Source2ModGameHookId HookDuck(GameHookHandler<MovementContext> handler, bool post) override;
        Source2ModGameHookId HookFriction(GameHookHandler<MovementContext> handler, bool post) override;
        Source2ModGameHookId HookFullWalkMove(GameHookHandler<FullWalkMoveContext> handler, bool post) override;
        Source2ModGameHookId HookGroundAccelerate(GameHookHandler<GroundAccelerateContext> handler, bool post) override;
        Source2ModGameHookId HookLadderMove(GameHookHandler<MovementContext> handler, bool post) override;
        Source2ModGameHookId HookMoveInit(GameHookHandler<MovementContext> handler, bool post) override;
        Source2ModGameHookId HookPlayerMove(GameHookHandler<MovementContext> handler, bool post) override;
        Source2ModGameHookId HookProcessMovement(GameHookHandler<MovementContext> handler, bool post) override;
        Source2ModGameHookId HookSetupMove(GameHookHandler<SetupMoveContext> handler, bool post) override;
        Source2ModGameHookId HookTryPlayerMove(GameHookHandler<TryPlayerMoveContext> handler, bool post) override;
        Source2ModGameHookId HookWalkMove(GameHookHandler<MovementContext> handler, bool post) override;
        Source2ModGameHookId HookWaterMove(GameHookHandler<MovementContext> handler, bool post) override;
        Source2ModGameHookId HookOnJumpLegacy(GameHookHandler<LegacyJumpContext> handler, bool post) override;
        Source2ModGameHookId HookOnJumpModern(GameHookHandler<ModernJumpContext> handler, bool post) override;
        Source2ModGameHookId HookCheckJumpButtonLegacy(GameHookHandler<LegacyJumpContext> handler, bool post) override;
        Source2ModGameHookId HookCheckJumpButtonModern(GameHookHandler<ModernJumpContext> handler, bool post) override;

        bool UnhookTakeDamage(Source2ModGameHookId id) override;
        bool UnhookTakeDamage(const GameHookHandler<TakeDamageContext>& handler, bool post) override;
        bool UnhookCanAcquire(Source2ModGameHookId id) override;
        bool UnhookCanAcquire(const GameHookHandler<CanAcquireContext>& handler, bool post) override;
        bool UnhookCanMove(Source2ModGameHookId id) override;
        bool UnhookCanMove(const GameHookHandler<CanMoveContext>& handler, bool post) override;
        bool UnhookCanUse(Source2ModGameHookId id) override;
        bool UnhookCanUse(const GameHookHandler<CanUseContext>& handler, bool post) override;
        bool UnhookPostThink(Source2ModGameHookId id) override;
        bool UnhookPostThink(const GameHookHandler<PostThinkContext>& handler, bool post) override;
        bool UnhookProcessUsercmds(Source2ModGameHookId id) override;
        bool UnhookProcessUsercmds(const GameHookHandler<ProcessUsercmdsContext>& handler, bool post) override;
        bool UnhookSimulateUserCommands(Source2ModGameHookId id) override;
        bool UnhookSimulateUserCommands(const GameHookHandler<SimulateUserCommandsContext>& handler, bool post) override;
        bool UnhookRunCommand(Source2ModGameHookId id) override;
        bool UnhookRunCommand(const GameHookHandler<RunCommandContext>& handler, bool post) override;
        bool UnhookAcceptInput(Source2ModGameHookId id) override;
        bool UnhookAcceptInput(const GameHookHandler<AcceptInputContext>& handler, bool post) override;
        bool UnhookTouch(Source2ModGameHookId id) override;
        bool UnhookTouch(const GameHookHandler<TouchContext>& handler, bool post) override;
        bool UnhookDropWeapon(Source2ModGameHookId id) override;
        bool UnhookDropWeapon(const GameHookHandler<DropWeaponContext>& handler, bool post) override;
        bool UnhookAirAccelerate(Source2ModGameHookId id) override;
        bool UnhookAirAccelerate(const GameHookHandler<AirAccelerateContext>& handler, bool post) override;
        bool UnhookAirMove(Source2ModGameHookId id) override;
        bool UnhookAirMove(const GameHookHandler<MovementContext>& handler, bool post) override;
        bool UnhookCanUnduck(Source2ModGameHookId id) override;
        bool UnhookCanUnduck(const GameHookHandler<MovementContext>& handler, bool post) override;
        bool UnhookCategorizePosition(Source2ModGameHookId id) override;
        bool UnhookCategorizePosition(const GameHookHandler<CategorizePositionContext>& handler, bool post) override;
        bool UnhookCheckFalling(Source2ModGameHookId id) override;
        bool UnhookCheckFalling(const GameHookHandler<MovementContext>& handler, bool post) override;
        bool UnhookCheckParameters(Source2ModGameHookId id) override;
        bool UnhookCheckParameters(const GameHookHandler<MovementContext>& handler, bool post) override;
        bool UnhookCheckVelocity(Source2ModGameHookId id) override;
        bool UnhookCheckVelocity(const GameHookHandler<CheckVelocityContext>& handler, bool post) override;
        bool UnhookCheckWater(Source2ModGameHookId id) override;
        bool UnhookCheckWater(const GameHookHandler<MovementContext>& handler, bool post) override;
        bool UnhookDuck(Source2ModGameHookId id) override;
        bool UnhookDuck(const GameHookHandler<MovementContext>& handler, bool post) override;
        bool UnhookFriction(Source2ModGameHookId id) override;
        bool UnhookFriction(const GameHookHandler<MovementContext>& handler, bool post) override;
        bool UnhookFullWalkMove(Source2ModGameHookId id) override;
        bool UnhookFullWalkMove(const GameHookHandler<FullWalkMoveContext>& handler, bool post) override;
        bool UnhookGroundAccelerate(Source2ModGameHookId id) override;
        bool UnhookGroundAccelerate(const GameHookHandler<GroundAccelerateContext>& handler, bool post) override;
        bool UnhookLadderMove(Source2ModGameHookId id) override;
        bool UnhookLadderMove(const GameHookHandler<MovementContext>& handler, bool post) override;
        bool UnhookMoveInit(Source2ModGameHookId id) override;
        bool UnhookMoveInit(const GameHookHandler<MovementContext>& handler, bool post) override;
        bool UnhookPlayerMove(Source2ModGameHookId id) override;
        bool UnhookPlayerMove(const GameHookHandler<MovementContext>& handler, bool post) override;
        bool UnhookProcessMovement(Source2ModGameHookId id) override;
        bool UnhookProcessMovement(const GameHookHandler<MovementContext>& handler, bool post) override;
        bool UnhookSetupMove(Source2ModGameHookId id) override;
        bool UnhookSetupMove(const GameHookHandler<SetupMoveContext>& handler, bool post) override;
        bool UnhookTryPlayerMove(Source2ModGameHookId id) override;
        bool UnhookTryPlayerMove(const GameHookHandler<TryPlayerMoveContext>& handler, bool post) override;
        bool UnhookWalkMove(Source2ModGameHookId id) override;
        bool UnhookWalkMove(const GameHookHandler<MovementContext>& handler, bool post) override;
        bool UnhookWaterMove(Source2ModGameHookId id) override;
        bool UnhookWaterMove(const GameHookHandler<MovementContext>& handler, bool post) override;
        bool UnhookOnJumpLegacy(Source2ModGameHookId id) override;
        bool UnhookOnJumpLegacy(const GameHookHandler<LegacyJumpContext>& handler, bool post) override;
        bool UnhookOnJumpModern(Source2ModGameHookId id) override;
        bool UnhookOnJumpModern(const GameHookHandler<ModernJumpContext>& handler, bool post) override;
        bool UnhookCheckJumpButtonLegacy(Source2ModGameHookId id) override;
        bool UnhookCheckJumpButtonLegacy(const GameHookHandler<LegacyJumpContext>& handler, bool post) override;
        bool UnhookCheckJumpButtonModern(Source2ModGameHookId id) override;
        bool UnhookCheckJumpButtonModern(const GameHookHandler<ModernJumpContext>& handler, bool post) override;
        bool IsAvailable(GameHook hook) override;

    private:
        std::vector<std::unique_ptr<IGameHook>> m_hooks; // indexed by GameHook
    };

    extern GameHooksManager gameHooksManager;

    /// "s2m hookdebug": logs every call of the named game hook ("all" for
    /// every one, "" / "off" to stop) -- each plugin's answer, what the core
    /// made of it and the return value.
    void SetHookDebug(const char* name);
    const char* GetHookDebug();
}
