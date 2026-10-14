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
        virtual bool Remove(GameHookId id) = 0;
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

        GameHookId HookTakeDamage(GameHookHandler<TakeDamageContext> handler, bool post) override;
        GameHookId HookCanAcquire(GameHookHandler<CanAcquireContext> handler, bool post) override;
        GameHookId HookCanMove(GameHookHandler<CanMoveContext> handler, bool post) override;
        GameHookId HookCanUse(GameHookHandler<CanUseContext> handler, bool post) override;
        GameHookId HookPostThink(GameHookHandler<PostThinkContext> handler, bool post) override;
        GameHookId HookProcessUsercmds(GameHookHandler<ProcessUsercmdsContext> handler, bool post) override;
        GameHookId HookSimulateUserCommands(GameHookHandler<SimulateUserCommandsContext> handler, bool post) override;
        GameHookId HookRunCommand(GameHookHandler<RunCommandContext> handler, bool post) override;
        GameHookId HookAcceptInput(GameHookHandler<AcceptInputContext> handler, bool post) override;
        GameHookId HookTouch(GameHookHandler<TouchContext> handler, bool post) override;
        GameHookId HookDropWeapon(GameHookHandler<DropWeaponContext> handler, bool post) override;
        GameHookId HookAirAccelerate(GameHookHandler<AirAccelerateContext> handler, bool post) override;
        GameHookId HookAirMove(GameHookHandler<MovementContext> handler, bool post) override;
        GameHookId HookCanUnduck(GameHookHandler<MovementContext> handler, bool post) override;
        GameHookId HookCategorizePosition(GameHookHandler<CategorizePositionContext> handler, bool post) override;
        GameHookId HookCheckFalling(GameHookHandler<MovementContext> handler, bool post) override;
        GameHookId HookCheckParameters(GameHookHandler<MovementContext> handler, bool post) override;
        GameHookId HookCheckVelocity(GameHookHandler<CheckVelocityContext> handler, bool post) override;
        GameHookId HookCheckWater(GameHookHandler<MovementContext> handler, bool post) override;
        GameHookId HookDuck(GameHookHandler<MovementContext> handler, bool post) override;
        GameHookId HookFriction(GameHookHandler<MovementContext> handler, bool post) override;
        GameHookId HookFullWalkMove(GameHookHandler<FullWalkMoveContext> handler, bool post) override;
        GameHookId HookGroundAccelerate(GameHookHandler<GroundAccelerateContext> handler, bool post) override;
        GameHookId HookLadderMove(GameHookHandler<MovementContext> handler, bool post) override;
        GameHookId HookMoveInit(GameHookHandler<MovementContext> handler, bool post) override;
        GameHookId HookPlayerMove(GameHookHandler<MovementContext> handler, bool post) override;
        GameHookId HookProcessMovement(GameHookHandler<MovementContext> handler, bool post) override;
        GameHookId HookSetupMove(GameHookHandler<SetupMoveContext> handler, bool post) override;
        GameHookId HookTryPlayerMove(GameHookHandler<TryPlayerMoveContext> handler, bool post) override;
        GameHookId HookWalkMove(GameHookHandler<MovementContext> handler, bool post) override;
        GameHookId HookWaterMove(GameHookHandler<MovementContext> handler, bool post) override;
        GameHookId HookOnJumpLegacy(GameHookHandler<LegacyJumpContext> handler, bool post) override;
        GameHookId HookOnJumpModern(GameHookHandler<ModernJumpContext> handler, bool post) override;
        GameHookId HookCheckJumpButtonLegacy(GameHookHandler<LegacyJumpContext> handler, bool post) override;
        GameHookId HookCheckJumpButtonModern(GameHookHandler<ModernJumpContext> handler, bool post) override;

        bool UnhookTakeDamage(GameHookId id) override;
        bool UnhookTakeDamage(const GameHookHandler<TakeDamageContext>& handler, bool post) override;
        bool UnhookCanAcquire(GameHookId id) override;
        bool UnhookCanAcquire(const GameHookHandler<CanAcquireContext>& handler, bool post) override;
        bool UnhookCanMove(GameHookId id) override;
        bool UnhookCanMove(const GameHookHandler<CanMoveContext>& handler, bool post) override;
        bool UnhookCanUse(GameHookId id) override;
        bool UnhookCanUse(const GameHookHandler<CanUseContext>& handler, bool post) override;
        bool UnhookPostThink(GameHookId id) override;
        bool UnhookPostThink(const GameHookHandler<PostThinkContext>& handler, bool post) override;
        bool UnhookProcessUsercmds(GameHookId id) override;
        bool UnhookProcessUsercmds(const GameHookHandler<ProcessUsercmdsContext>& handler, bool post) override;
        bool UnhookSimulateUserCommands(GameHookId id) override;
        bool UnhookSimulateUserCommands(const GameHookHandler<SimulateUserCommandsContext>& handler, bool post) override;
        bool UnhookRunCommand(GameHookId id) override;
        bool UnhookRunCommand(const GameHookHandler<RunCommandContext>& handler, bool post) override;
        bool UnhookAcceptInput(GameHookId id) override;
        bool UnhookAcceptInput(const GameHookHandler<AcceptInputContext>& handler, bool post) override;
        bool UnhookTouch(GameHookId id) override;
        bool UnhookTouch(const GameHookHandler<TouchContext>& handler, bool post) override;
        bool UnhookDropWeapon(GameHookId id) override;
        bool UnhookDropWeapon(const GameHookHandler<DropWeaponContext>& handler, bool post) override;
        bool UnhookAirAccelerate(GameHookId id) override;
        bool UnhookAirAccelerate(const GameHookHandler<AirAccelerateContext>& handler, bool post) override;
        bool UnhookAirMove(GameHookId id) override;
        bool UnhookAirMove(const GameHookHandler<MovementContext>& handler, bool post) override;
        bool UnhookCanUnduck(GameHookId id) override;
        bool UnhookCanUnduck(const GameHookHandler<MovementContext>& handler, bool post) override;
        bool UnhookCategorizePosition(GameHookId id) override;
        bool UnhookCategorizePosition(const GameHookHandler<CategorizePositionContext>& handler, bool post) override;
        bool UnhookCheckFalling(GameHookId id) override;
        bool UnhookCheckFalling(const GameHookHandler<MovementContext>& handler, bool post) override;
        bool UnhookCheckParameters(GameHookId id) override;
        bool UnhookCheckParameters(const GameHookHandler<MovementContext>& handler, bool post) override;
        bool UnhookCheckVelocity(GameHookId id) override;
        bool UnhookCheckVelocity(const GameHookHandler<CheckVelocityContext>& handler, bool post) override;
        bool UnhookCheckWater(GameHookId id) override;
        bool UnhookCheckWater(const GameHookHandler<MovementContext>& handler, bool post) override;
        bool UnhookDuck(GameHookId id) override;
        bool UnhookDuck(const GameHookHandler<MovementContext>& handler, bool post) override;
        bool UnhookFriction(GameHookId id) override;
        bool UnhookFriction(const GameHookHandler<MovementContext>& handler, bool post) override;
        bool UnhookFullWalkMove(GameHookId id) override;
        bool UnhookFullWalkMove(const GameHookHandler<FullWalkMoveContext>& handler, bool post) override;
        bool UnhookGroundAccelerate(GameHookId id) override;
        bool UnhookGroundAccelerate(const GameHookHandler<GroundAccelerateContext>& handler, bool post) override;
        bool UnhookLadderMove(GameHookId id) override;
        bool UnhookLadderMove(const GameHookHandler<MovementContext>& handler, bool post) override;
        bool UnhookMoveInit(GameHookId id) override;
        bool UnhookMoveInit(const GameHookHandler<MovementContext>& handler, bool post) override;
        bool UnhookPlayerMove(GameHookId id) override;
        bool UnhookPlayerMove(const GameHookHandler<MovementContext>& handler, bool post) override;
        bool UnhookProcessMovement(GameHookId id) override;
        bool UnhookProcessMovement(const GameHookHandler<MovementContext>& handler, bool post) override;
        bool UnhookSetupMove(GameHookId id) override;
        bool UnhookSetupMove(const GameHookHandler<SetupMoveContext>& handler, bool post) override;
        bool UnhookTryPlayerMove(GameHookId id) override;
        bool UnhookTryPlayerMove(const GameHookHandler<TryPlayerMoveContext>& handler, bool post) override;
        bool UnhookWalkMove(GameHookId id) override;
        bool UnhookWalkMove(const GameHookHandler<MovementContext>& handler, bool post) override;
        bool UnhookWaterMove(GameHookId id) override;
        bool UnhookWaterMove(const GameHookHandler<MovementContext>& handler, bool post) override;
        bool UnhookOnJumpLegacy(GameHookId id) override;
        bool UnhookOnJumpLegacy(const GameHookHandler<LegacyJumpContext>& handler, bool post) override;
        bool UnhookOnJumpModern(GameHookId id) override;
        bool UnhookOnJumpModern(const GameHookHandler<ModernJumpContext>& handler, bool post) override;
        bool UnhookCheckJumpButtonLegacy(GameHookId id) override;
        bool UnhookCheckJumpButtonLegacy(const GameHookHandler<LegacyJumpContext>& handler, bool post) override;
        bool UnhookCheckJumpButtonModern(GameHookId id) override;
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
