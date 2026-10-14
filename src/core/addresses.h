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
#include "utils/log.h"

#include <dynlibutils/memaddr.hpp>
#include <dynlibutils/module.hpp>
#include <entity2/variant.h>
#include <platform.h>
#include <playerslot.h>
#include <source2mod/ISource2ModAddresses.h>

/// Resolves a gamedata signature by its entry name: the pattern and the module
/// (the entry's "library") both come from the game config.
#define RESOLVE_SIG(name, variable)                                   \
{                                                                     \
    auto addr = FindSignature(name);                                  \
                                                                      \
    if (!addr)                                                        \
    {                                                                 \
        FP_DEBUG("Failed to find '{}'", name);                        \
        return false;                                                 \
    }                                                                 \
                                                                      \
    variable = addr;                                                  \
                                                                      \
    FP_DEBUG("Found '{}' at {}", name, fmt::ptr(variable.GetPtr()));  \
}

/// Same as RESOLVE_SIG, but a miss is not fatal: the address is left null and
/// initialization carries on. For functions we only have a signature for on
/// some platforms -- the caller is expected to null-check before using it.
#define RESOLVE_SIG_OPTIONAL(name, variable)                          \
{                                                                     \
    auto addr = FindSignature(name);                                  \
                                                                      \
    if (!addr)                                                        \
    {                                                                 \
        FP_DEBUG("Optional '{}' not found, leaving it unresolved", name); \
    }                                                                 \
    else                                                              \
    {                                                                 \
        variable = addr;                                              \
        FP_DEBUG("Found '{}' at {}", name, fmt::ptr(variable.GetPtr())); \
    }                                                                 \
}

class CEntityInstance;
class CEntityIOOutput;
class CEntitySystem;
class CEntityKeyValues;
class CBaseEntity;
class CBasePlayerWeapon;
class CBaseModelEntity;
class CBasePlayerPawn;
class CBasePlayerController;
class CGameRules;
class CCSPlayerController;
class IGameEventListener2;
class IGameEventManager2;
class IEntityFindFilter;

namespace addresses
{
    bool Initialize();

    /// Scans the module named by a gamedata entry for its signature; null on a miss.
    DynLibUtils::CMemory FindSignature(const char* pszName);

    class Addresses : public ISource2ModAddresses {
    public:
        DynLibUtils::CModule& GetOrLoadModule(void* ptr);
    public:
        CBaseEntity_CreateEntityByName_t CBaseEntity_CreateEntityByName() override;
        CBaseEntity_DispatchSpawn_t CBaseEntity_DispatchSpawn() override;
        CBaseEntity_TakeDamageOld_t CBaseEntity_TakeDamageOld() override;
        CBaseModelEntity_SetModel_t CBaseModelEntity_SetModel() override;
        CBasePlayerController_SetPawn_t CBasePlayerController_SetPawn() override;
        CBasePlayerPawn_SnapViewAngles_t CBasePlayerPawn_SnapViewAngles() override;
        CCSGameRules_TerminateRound_t CCSGameRules_TerminateRound() override;
        CPlayer_WeaponServices_Destroy_t CPlayer_WeaponServices_Destroy() override;
        LegacyGameEventListener_t LegacyGameEventListener() override;
        CCSPlayerController_SwitchTeam_t CCSPlayerController_SwitchTeam() override;
        CEntityInstance_AcceptInput_t CEntityInstance_AcceptInput() override;
        CEntityIOOutput_FireOutputInternal_t CEntityIOOutput_FireOutputInternal() override;
        CEntitySystem_AddEntityIOEvent_t CEntitySystem_AddEntityIOEvent() override;
        CGameEntitySystem_FindEntityByClassName_t CGameEntitySystem_FindEntityByClassName() override;
        CGameEntitySystem_FindEntityByName_t CGameEntitySystem_FindEntityByName() override;
        CTakeDamageInfo_CTakeDamageInfo_t CTakeDamageInfo_CTakeDamageInfo() override;
        INetworkMessageProcessingPreFilter_FilterMessage_t INetworkMessageProcessingPreFilter_FilterMessage() override;

        // Ported from SwiftlyS2.
        CEntityIdentity_AcceptInput_t CEntityIdentity_AcceptInput() override;
        CCSPlayer_ItemServices_CanAcquire_t CCSPlayer_ItemServices_CanAcquire() override;
        CCSPlayerPawnBase_CanMove_t CCSPlayerPawnBase_CanMove() override;
        CCSPlayerController_ProcessUserCmd_t CCSPlayerController_ProcessUserCmd() override;
        CBasePlayerController_OnSimulateUserCommands_t CBasePlayerController_OnSimulateUserCommands() override;
        CCSPlayer_MovementServices_AirAccelerate_t CCSPlayer_MovementServices_AirAccelerate() override;
        CCSPlayer_MovementServices_AirMove_t CCSPlayer_MovementServices_AirMove() override;
        CCSPlayer_MovementServices_CanUnduck_t CCSPlayer_MovementServices_CanUnduck() override;
        CCSPlayer_MovementServices_CategorizePosition_t CCSPlayer_MovementServices_CategorizePosition() override;
        CCSPlayer_MovementServices_CheckFalling_t CCSPlayer_MovementServices_CheckFalling() override;
        CCSPlayer_MovementServices_CheckParameters_t CCSPlayer_MovementServices_CheckParameters() override;
        CCSPlayer_MovementServices_CheckVelocity_t CCSPlayer_MovementServices_CheckVelocity() override;
        CCSPlayer_MovementServices_CheckWater_t CCSPlayer_MovementServices_CheckWater() override;
        CCSPlayer_MovementServices_Duck_t CCSPlayer_MovementServices_Duck() override;
        CCSPlayer_MovementServices_Friction_t CCSPlayer_MovementServices_Friction() override;
        CCSPlayer_MovementServices_FullWalkMove_t CCSPlayer_MovementServices_FullWalkMove() override;
        CCSPlayer_MovementServices_GroundAccelerate_t CCSPlayer_MovementServices_GroundAccelerate() override;
        CCSPlayer_MovementServices_LadderMove_t CCSPlayer_MovementServices_LadderMove() override;
        CCSPlayer_MovementServices_MoveInit_t CCSPlayer_MovementServices_MoveInit() override;
        CCSPlayer_MovementServices_PlayerMove_t CCSPlayer_MovementServices_PlayerMove() override;
        CCSPlayer_MovementServices_ProcessMovement_t CCSPlayer_MovementServices_ProcessMovement() override;
        CCSPlayer_MovementServices_SetupMove_t CCSPlayer_MovementServices_SetupMove() override;
        CCSPlayer_MovementServices_TryPlayerMove_t CCSPlayer_MovementServices_TryPlayerMove() override;
        CCSPlayer_MovementServices_WalkMove_t CCSPlayer_MovementServices_WalkMove() override;
        CCSPlayer_MovementServices_WaterMove_t CCSPlayer_MovementServices_WaterMove() override;
        CCSPlayerLegacyJump_OnJump_t CCSPlayerLegacyJump_OnJump() override;
        CCSPlayerModernJump_OnJump_t CCSPlayerModernJump_OnJump() override;
        CCSPlayerLegacyJump_CheckJumpButton_t CCSPlayerLegacyJump_CheckJumpButton() override;
        CCSPlayerModernJump_CheckJumpButton_t CCSPlayerModernJump_CheckJumpButton() override;
        CAttributeList_SetOrAddAttributeValueByName_t CAttributeList_SetOrAddAttributeValueByName() override;
        CDecoyProjectile_EmitGrenade_t CDecoyProjectile_EmitGrenade() override;
        CFlashbangProjectile_EmitGrenade_t CFlashbangProjectile_EmitGrenade() override;
        CHEGrenadeProjectile_EmitGrenade_t CHEGrenadeProjectile_EmitGrenade() override;
        CMolotovProjectile_EmitGrenade_t CMolotovProjectile_EmitGrenade() override;
        CSmokeGrenadeProjectile_EmitGrenade_t CSmokeGrenadeProjectile_EmitGrenade() override;
        CCSPlayerPawn_PostThink_t CCSPlayerPawn_PostThink() override;
        UTIL_Remove_t UTIL_Remove() override;
        DispatchParticleEffect_t DispatchParticleEffect() override;
        GetWeaponCSDataFromKey_t GetWeaponCSDataFromKey() override;
        CSource2Server_GetNavMeshData_t CSource2Server_GetNavMeshData() override;
        CLoggingSystem_LogDirect_t CLoggingSystem_LogDirect() override;
        Cmd_ExecuteCommand_t Cmd_ExecuteCommand() override;
        CCSPlayer_ItemServices_GiveNamedItem_t CCSPlayer_ItemServices_GiveNamedItem() override;
    public:
        // Raw addresses. A function pointer is not portably convertible to
        // void* and back, so what a signature scan produces is kept as the
        // address it is; the getters below put the type on at the point of use,
        // and KHook's Configure() takes the void* straight from GetPtr().
        DynLibUtils::CMemory CreateEntityByName;
        DynLibUtils::CMemory DispatchSpawn;
        DynLibUtils::CMemory TakeDamageOld;
        DynLibUtils::CMemory SetModel;
        DynLibUtils::CMemory SetPawn;
        DynLibUtils::CMemory SnapViewAngles;
        DynLibUtils::CMemory TerminateRound;
        DynLibUtils::CMemory Destroy;
        // Suffixed unlike its neighbours: the interface getter for this one has
        // no class prefix (ISource2ModAddresses::LegacyGameEventListener()), so a
        // bare member of the same name would redeclare it.
        DynLibUtils::CMemory LegacyGameEventListenerAddr;
        DynLibUtils::CMemory SwitchTeam;
        DynLibUtils::CMemory AcceptInput;
        DynLibUtils::CMemory FireOutputInternal;
        DynLibUtils::CMemory AddEntityIOEvent;
        DynLibUtils::CMemory FindEntityByClassName;
        DynLibUtils::CMemory FindEntityByName;
        DynLibUtils::CMemory CTakeDamageInfo;
        DynLibUtils::CMemory FilterMessage;

        DynLibUtils::CMemory IdentityAcceptInput;
        DynLibUtils::CMemory CanAcquire;
        DynLibUtils::CMemory CanMove;
        DynLibUtils::CMemory ProcessUserCmd;
        DynLibUtils::CMemory OnSimulateUserCommands;
        DynLibUtils::CMemory AirAccelerate;
        DynLibUtils::CMemory AirMove;
        DynLibUtils::CMemory CanUnduck;
        DynLibUtils::CMemory CategorizePosition;
        DynLibUtils::CMemory CheckFalling;
        DynLibUtils::CMemory CheckParameters;
        DynLibUtils::CMemory CheckVelocity;
        DynLibUtils::CMemory CheckWater;
        DynLibUtils::CMemory Duck;
        DynLibUtils::CMemory Friction;
        DynLibUtils::CMemory FullWalkMove;
        DynLibUtils::CMemory GroundAccelerate;
        DynLibUtils::CMemory LadderMove;
        DynLibUtils::CMemory MoveInit;
        DynLibUtils::CMemory PlayerMove;
        DynLibUtils::CMemory ProcessMovement;
        DynLibUtils::CMemory SetupMove;
        DynLibUtils::CMemory TryPlayerMove;
        DynLibUtils::CMemory WalkMove;
        DynLibUtils::CMemory WaterMove;
        DynLibUtils::CMemory OnJumpLegacy;
        DynLibUtils::CMemory OnJumpModern;
        DynLibUtils::CMemory CheckJumpButtonLegacy;
        DynLibUtils::CMemory CheckJumpButtonModern;
        DynLibUtils::CMemory SetOrAddAttributeValueByName;
        DynLibUtils::CMemory EmitDecoy;
        DynLibUtils::CMemory EmitFlashbang;
        DynLibUtils::CMemory EmitHEGrenade;
        DynLibUtils::CMemory EmitMolotov;
        DynLibUtils::CMemory EmitSmoke;
        DynLibUtils::CMemory PostThink;
        // Suffixed like LegacyGameEventListenerAddr: these getters carry no
        // class prefix, so a bare member would redeclare them.
        DynLibUtils::CMemory UTIL_RemoveAddr;
        DynLibUtils::CMemory DispatchParticleEffectAddr;
        DynLibUtils::CMemory GetWeaponCSDataFromKeyAddr;
        DynLibUtils::CMemory GetNavMeshData;
        DynLibUtils::CMemory LogDirect;
        DynLibUtils::CMemory Cmd_ExecuteCommandAddr;
        DynLibUtils::CMemory GiveNamedItem;
    private:
        std::unordered_map<uintptr_t, DynLibUtils::CModule> m_Modules;
    };

    extern Addresses source2modAddresses;
}
