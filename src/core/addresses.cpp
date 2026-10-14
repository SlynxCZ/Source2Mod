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
#include "addresses.h"

#include "shared.h"

namespace addresses
{
    Addresses source2modAddresses;

    DynLibUtils::CMemory FindSignature(const char* pszName)
    {
        // The module comes from the entry's "library" field, so tier0 and
        // engine2 entries need no interface pointer to stand in for them.
        DynLibUtils::CModule* pModule = shared::g_pGameConfig->GetModule(pszName);
        const char* pszSignature = shared::g_pGameConfig->GetSignature(pszName);
        if (!pModule || !pszSignature)
            return {};

        return pModule->FindPattern(DynLibUtils::ParsePattern(pszSignature));
    }

    bool Initialize()
    {
        RESOLVE_SIG("UTIL::CreateEntityByName", source2modAddresses.CreateEntityByName);
        RESOLVE_SIG("CBaseEntity::DispatchSpawn", source2modAddresses.DispatchSpawn);
        RESOLVE_SIG("CBaseEntity::TakeDamageOld", source2modAddresses.TakeDamageOld);
        RESOLVE_SIG("CBaseModelEntity::SetModel", source2modAddresses.SetModel);
        RESOLVE_SIG("CBasePlayerController::SetPawn", source2modAddresses.SetPawn);
        RESOLVE_SIG("CBasePlayerPawn::SnapViewAngles", source2modAddresses.SnapViewAngles);
        RESOLVE_SIG("CCSGameRules::TerminateRound", source2modAddresses.TerminateRound);
        RESOLVE_SIG("CCSPlayer_WeaponServices::Destroy", source2modAddresses.Destroy);
        RESOLVE_SIG("LegacyGameEventListener", source2modAddresses.LegacyGameEventListenerAddr);
        RESOLVE_SIG("CCSPlayerController::SwitchTeam", source2modAddresses.SwitchTeam);
        RESOLVE_SIG("CEntityInstance::AcceptInput", source2modAddresses.AcceptInput);
        RESOLVE_SIG("CEntityIOOutput::FireOutputInternal", source2modAddresses.FireOutputInternal);
        RESOLVE_SIG("CEntitySystem::AddEntityIOEvent", source2modAddresses.AddEntityIOEvent);
        RESOLVE_SIG("CGameEntitySystem::FindEntityByClassName", source2modAddresses.FindEntityByClassName);
        RESOLVE_SIG("CGameEntitySystem::FindEntityByName", source2modAddresses.FindEntityByName);
        RESOLVE_SIG("CTakeDamageInfo::Constructor", source2modAddresses.CTakeDamageInfo);

        // FilterMessage is the only virtual of CServerSideClient's pre-filter
        // base; the base's offset comes out of the RTTI (+48 Linux, +8 Windows).
        {
            DynLibUtils::CModule libengine(g_pEngineServer);
            auto pVTable = libengine.GetVirtualTableByBase("CServerSideClient", "INetworkMessageProcessingPreFilter");
            if (!pVTable)
            {
                FP_DEBUG("Failed to find '{}'", "CServerSideClient::INetworkMessageProcessingPreFilter");
                return false;
            }

            source2modAddresses.FilterMessage = DynLibUtils::CMemory(pVTable.RCast<void**>()[0]);
        }


        // Called by the SDK's schema helpers (CAttributeList, the grenade
        // projectiles, CBaseEntity::Remove, ...). The helpers call straight
        // through, so these must resolve like the ones above.
        RESOLVE_SIG("CAttributeList::SetOrAddAttributeValueByName", source2modAddresses.SetOrAddAttributeValueByName);
        RESOLVE_SIG("CDecoyProjectile::EmitGrenade", source2modAddresses.EmitDecoy);
        RESOLVE_SIG("CFlashbangProjectile::EmitGrenade", source2modAddresses.EmitFlashbang);
        RESOLVE_SIG("CHEGrenadeProjectile::EmitGrenade", source2modAddresses.EmitHEGrenade);
        RESOLVE_SIG("CMolotovProjectile::EmitGrenade", source2modAddresses.EmitMolotov);
        RESOLVE_SIG("CSmokeGrenadeProjectile::EmitGrenade", source2modAddresses.EmitSmoke);
        RESOLVE_SIG("CCSPlayerPawnBase::CanMove", source2modAddresses.CanMove);
        RESOLVE_SIG("CCSPlayerPawn::PostThink", source2modAddresses.PostThink);
        RESOLVE_SIG("UTIL::Remove", source2modAddresses.UTIL_RemoveAddr);
        RESOLVE_SIG("DispatchParticleEffect", source2modAddresses.DispatchParticleEffectAddr);
        RESOLVE_SIG("GetWeaponCSDataFromKey", source2modAddresses.GetWeaponCSDataFromKeyAddr);
        RESOLVE_SIG("CCSPlayer_ItemServices::GiveNamedItem", source2modAddresses.GiveNamedItem);

        // Hook targets exposed for plugins only; nothing in Source2Mod calls
        // them. Resolved optionally so a pattern that stops matching after a
        // game update does not block startup -- the getter returns null.
        RESOLVE_SIG_OPTIONAL("CEntityIdentity::AcceptInput", source2modAddresses.IdentityAcceptInput);
        RESOLVE_SIG_OPTIONAL("CCSPlayer_ItemServices::CanAcquire", source2modAddresses.CanAcquire);
        RESOLVE_SIG_OPTIONAL("CCSPlayerController::ProcessUserCmd", source2modAddresses.ProcessUserCmd);
        RESOLVE_SIG_OPTIONAL("CBasePlayerController::OnSimulateUserCommands", source2modAddresses.OnSimulateUserCommands);
        RESOLVE_SIG_OPTIONAL("CCSPlayer_MovementServices::AirAccelerate", source2modAddresses.AirAccelerate);
        RESOLVE_SIG_OPTIONAL("CCSPlayer_MovementServices::AirMove", source2modAddresses.AirMove);
        RESOLVE_SIG_OPTIONAL("CCSPlayer_MovementServices::CanUnduck", source2modAddresses.CanUnduck);
        RESOLVE_SIG_OPTIONAL("CCSPlayer_MovementServices::CategorizePosition", source2modAddresses.CategorizePosition);
        RESOLVE_SIG_OPTIONAL("CCSPlayer_MovementServices::CheckFalling", source2modAddresses.CheckFalling);
        RESOLVE_SIG_OPTIONAL("CCSPlayer_MovementServices::CheckParameters", source2modAddresses.CheckParameters);
        RESOLVE_SIG_OPTIONAL("CCSPlayer_MovementServices::CheckVelocity", source2modAddresses.CheckVelocity);
        RESOLVE_SIG_OPTIONAL("CCSPlayer_MovementServices::CheckWater", source2modAddresses.CheckWater);
        RESOLVE_SIG_OPTIONAL("CCSPlayer_MovementServices::Duck", source2modAddresses.Duck);
        RESOLVE_SIG_OPTIONAL("CCSPlayer_MovementServices::Friction", source2modAddresses.Friction);
        RESOLVE_SIG_OPTIONAL("CCSPlayer_MovementServices::FullWalkMove", source2modAddresses.FullWalkMove);
        RESOLVE_SIG_OPTIONAL("CCSPlayer_MovementServices::GroundAccelerate", source2modAddresses.GroundAccelerate);
        RESOLVE_SIG_OPTIONAL("CCSPlayer_MovementServices::LadderMove", source2modAddresses.LadderMove);
        RESOLVE_SIG_OPTIONAL("CCSPlayer_MovementServices::MoveInit", source2modAddresses.MoveInit);
        RESOLVE_SIG_OPTIONAL("CCSPlayer_MovementServices::PlayerMove", source2modAddresses.PlayerMove);
        RESOLVE_SIG_OPTIONAL("CCSPlayer_MovementServices::ProcessMovement", source2modAddresses.ProcessMovement);
        RESOLVE_SIG_OPTIONAL("CCSPlayer_MovementServices::SetupMove", source2modAddresses.SetupMove);
        RESOLVE_SIG_OPTIONAL("CCSPlayer_MovementServices::TryPlayerMove", source2modAddresses.TryPlayerMove);
        RESOLVE_SIG_OPTIONAL("CCSPlayer_MovementServices::WalkMove", source2modAddresses.WalkMove);
        RESOLVE_SIG_OPTIONAL("CCSPlayer_MovementServices::WaterMove", source2modAddresses.WaterMove);
        RESOLVE_SIG_OPTIONAL("CCSPlayerLegacyJump::OnJump", source2modAddresses.OnJumpLegacy);
        RESOLVE_SIG_OPTIONAL("CCSPlayerModernJump::OnJump", source2modAddresses.OnJumpModern);
        RESOLVE_SIG_OPTIONAL("CCSPlayerLegacyJump::CheckJumpButton", source2modAddresses.CheckJumpButtonLegacy);
        RESOLVE_SIG_OPTIONAL("CCSPlayerModernJump::CheckJumpButton", source2modAddresses.CheckJumpButtonModern);
        // CCSNavArea resolves this one itself and copes with a miss.
        RESOLVE_SIG_OPTIONAL("CSource2Server::GetNavMeshData", source2modAddresses.GetNavMeshData);
        RESOLVE_SIG_OPTIONAL("CLoggingSystem::LogDirect", source2modAddresses.LogDirect);
        RESOLVE_SIG_OPTIONAL("Cmd_ExecuteCommand", source2modAddresses.Cmd_ExecuteCommandAddr);

        return true;
    }

    DynLibUtils::CModule& Addresses::GetOrLoadModule(void* ptr)
    {
        uintptr_t key = reinterpret_cast<uintptr_t>(ptr);

        auto it = m_Modules.find(key);
        if (it != m_Modules.end())
            return it->second;

        auto [iter, _] = m_Modules.emplace(key, DynLibUtils::CModule(DynLibUtils::CMemory(ptr)));
        return iter->second;
    }

    CBaseEntity_CreateEntityByName_t Addresses::CBaseEntity_CreateEntityByName()
    {
        return CreateEntityByName.RCast<CBaseEntity_CreateEntityByName_t>();
    }

    CBaseEntity_DispatchSpawn_t Addresses::CBaseEntity_DispatchSpawn()
    {
        return DispatchSpawn.RCast<CBaseEntity_DispatchSpawn_t>();
    }

    CBaseEntity_TakeDamageOld_t Addresses::CBaseEntity_TakeDamageOld()
    {
        return TakeDamageOld.RCast<CBaseEntity_TakeDamageOld_t>();
    }

    CBaseModelEntity_SetModel_t Addresses::CBaseModelEntity_SetModel()
    {
        return SetModel.RCast<CBaseModelEntity_SetModel_t>();
    }

    CBasePlayerController_SetPawn_t Addresses::CBasePlayerController_SetPawn()
    {
        return SetPawn.RCast<CBasePlayerController_SetPawn_t>();
    }

    CBasePlayerPawn_SnapViewAngles_t Addresses::CBasePlayerPawn_SnapViewAngles()
    {
        return SnapViewAngles.RCast<CBasePlayerPawn_SnapViewAngles_t>();
    }

    CCSGameRules_TerminateRound_t Addresses::CCSGameRules_TerminateRound()
    {
        return TerminateRound.RCast<CCSGameRules_TerminateRound_t>();
    }

    CPlayer_WeaponServices_Destroy_t Addresses::CPlayer_WeaponServices_Destroy()
    {
        return Destroy.RCast<CPlayer_WeaponServices_Destroy_t>();
    }

    LegacyGameEventListener_t Addresses::LegacyGameEventListener()
    {
        return LegacyGameEventListenerAddr.RCast<LegacyGameEventListener_t>();
    }

    CCSPlayerController_SwitchTeam_t Addresses::CCSPlayerController_SwitchTeam()
    {
        return SwitchTeam.RCast<CCSPlayerController_SwitchTeam_t>();
    }

    CEntityInstance_AcceptInput_t Addresses::CEntityInstance_AcceptInput()
    {
        return AcceptInput.RCast<CEntityInstance_AcceptInput_t>();
    }

    CEntityIOOutput_FireOutputInternal_t Addresses::CEntityIOOutput_FireOutputInternal()
    {
        return FireOutputInternal.RCast<CEntityIOOutput_FireOutputInternal_t>();
    }

    CEntitySystem_AddEntityIOEvent_t Addresses::CEntitySystem_AddEntityIOEvent()
    {
        return AddEntityIOEvent.RCast<CEntitySystem_AddEntityIOEvent_t>();
    }

    CGameEntitySystem_FindEntityByClassName_t Addresses::CGameEntitySystem_FindEntityByClassName()
    {
        return FindEntityByClassName.RCast<CGameEntitySystem_FindEntityByClassName_t>();
    }

    CGameEntitySystem_FindEntityByName_t Addresses::CGameEntitySystem_FindEntityByName()
    {
        return FindEntityByName.RCast<CGameEntitySystem_FindEntityByName_t>();
    }

    CTakeDamageInfo_CTakeDamageInfo_t Addresses::CTakeDamageInfo_CTakeDamageInfo()
    {
        return CTakeDamageInfo.RCast<CTakeDamageInfo_CTakeDamageInfo_t>();
    }

    INetworkMessageProcessingPreFilter_FilterMessage_t Addresses::INetworkMessageProcessingPreFilter_FilterMessage()
    {
        return FilterMessage.RCast<INetworkMessageProcessingPreFilter_FilterMessage_t>();
    }

    CEntityIdentity_AcceptInput_t Addresses::CEntityIdentity_AcceptInput()
    {
        return IdentityAcceptInput.RCast<CEntityIdentity_AcceptInput_t>();
    }

    CCSPlayer_ItemServices_CanAcquire_t Addresses::CCSPlayer_ItemServices_CanAcquire()
    {
        return CanAcquire.RCast<CCSPlayer_ItemServices_CanAcquire_t>();
    }

    CCSPlayerPawnBase_CanMove_t Addresses::CCSPlayerPawnBase_CanMove()
    {
        return CanMove.RCast<CCSPlayerPawnBase_CanMove_t>();
    }

    CCSPlayerController_ProcessUserCmd_t Addresses::CCSPlayerController_ProcessUserCmd()
    {
        return ProcessUserCmd.RCast<CCSPlayerController_ProcessUserCmd_t>();
    }

    CBasePlayerController_OnSimulateUserCommands_t Addresses::CBasePlayerController_OnSimulateUserCommands()
    {
        return OnSimulateUserCommands.RCast<CBasePlayerController_OnSimulateUserCommands_t>();
    }

    CCSPlayer_MovementServices_AirAccelerate_t Addresses::CCSPlayer_MovementServices_AirAccelerate()
    {
        return AirAccelerate.RCast<CCSPlayer_MovementServices_AirAccelerate_t>();
    }

    CCSPlayer_MovementServices_AirMove_t Addresses::CCSPlayer_MovementServices_AirMove()
    {
        return AirMove.RCast<CCSPlayer_MovementServices_AirMove_t>();
    }

    CCSPlayer_MovementServices_CanUnduck_t Addresses::CCSPlayer_MovementServices_CanUnduck()
    {
        return CanUnduck.RCast<CCSPlayer_MovementServices_CanUnduck_t>();
    }

    CCSPlayer_MovementServices_CategorizePosition_t Addresses::CCSPlayer_MovementServices_CategorizePosition()
    {
        return CategorizePosition.RCast<CCSPlayer_MovementServices_CategorizePosition_t>();
    }

    CCSPlayer_MovementServices_CheckFalling_t Addresses::CCSPlayer_MovementServices_CheckFalling()
    {
        return CheckFalling.RCast<CCSPlayer_MovementServices_CheckFalling_t>();
    }

    CCSPlayer_MovementServices_CheckParameters_t Addresses::CCSPlayer_MovementServices_CheckParameters()
    {
        return CheckParameters.RCast<CCSPlayer_MovementServices_CheckParameters_t>();
    }

    CCSPlayer_MovementServices_CheckVelocity_t Addresses::CCSPlayer_MovementServices_CheckVelocity()
    {
        return CheckVelocity.RCast<CCSPlayer_MovementServices_CheckVelocity_t>();
    }

    CCSPlayer_MovementServices_CheckWater_t Addresses::CCSPlayer_MovementServices_CheckWater()
    {
        return CheckWater.RCast<CCSPlayer_MovementServices_CheckWater_t>();
    }

    CCSPlayer_MovementServices_Duck_t Addresses::CCSPlayer_MovementServices_Duck()
    {
        return Duck.RCast<CCSPlayer_MovementServices_Duck_t>();
    }

    CCSPlayer_MovementServices_Friction_t Addresses::CCSPlayer_MovementServices_Friction()
    {
        return Friction.RCast<CCSPlayer_MovementServices_Friction_t>();
    }

    CCSPlayer_MovementServices_FullWalkMove_t Addresses::CCSPlayer_MovementServices_FullWalkMove()
    {
        return FullWalkMove.RCast<CCSPlayer_MovementServices_FullWalkMove_t>();
    }

    CCSPlayer_MovementServices_GroundAccelerate_t Addresses::CCSPlayer_MovementServices_GroundAccelerate()
    {
        return GroundAccelerate.RCast<CCSPlayer_MovementServices_GroundAccelerate_t>();
    }

    CCSPlayer_MovementServices_LadderMove_t Addresses::CCSPlayer_MovementServices_LadderMove()
    {
        return LadderMove.RCast<CCSPlayer_MovementServices_LadderMove_t>();
    }

    CCSPlayer_MovementServices_MoveInit_t Addresses::CCSPlayer_MovementServices_MoveInit()
    {
        return MoveInit.RCast<CCSPlayer_MovementServices_MoveInit_t>();
    }

    CCSPlayer_MovementServices_PlayerMove_t Addresses::CCSPlayer_MovementServices_PlayerMove()
    {
        return PlayerMove.RCast<CCSPlayer_MovementServices_PlayerMove_t>();
    }

    CCSPlayer_MovementServices_ProcessMovement_t Addresses::CCSPlayer_MovementServices_ProcessMovement()
    {
        return ProcessMovement.RCast<CCSPlayer_MovementServices_ProcessMovement_t>();
    }

    CCSPlayer_MovementServices_SetupMove_t Addresses::CCSPlayer_MovementServices_SetupMove()
    {
        return SetupMove.RCast<CCSPlayer_MovementServices_SetupMove_t>();
    }

    CCSPlayer_MovementServices_TryPlayerMove_t Addresses::CCSPlayer_MovementServices_TryPlayerMove()
    {
        return TryPlayerMove.RCast<CCSPlayer_MovementServices_TryPlayerMove_t>();
    }

    CCSPlayer_MovementServices_WalkMove_t Addresses::CCSPlayer_MovementServices_WalkMove()
    {
        return WalkMove.RCast<CCSPlayer_MovementServices_WalkMove_t>();
    }

    CCSPlayer_MovementServices_WaterMove_t Addresses::CCSPlayer_MovementServices_WaterMove()
    {
        return WaterMove.RCast<CCSPlayer_MovementServices_WaterMove_t>();
    }

    CCSPlayerLegacyJump_OnJump_t Addresses::CCSPlayerLegacyJump_OnJump()
    {
        return OnJumpLegacy.RCast<CCSPlayerLegacyJump_OnJump_t>();
    }

    CCSPlayerModernJump_OnJump_t Addresses::CCSPlayerModernJump_OnJump()
    {
        return OnJumpModern.RCast<CCSPlayerModernJump_OnJump_t>();
    }

    CCSPlayerLegacyJump_CheckJumpButton_t Addresses::CCSPlayerLegacyJump_CheckJumpButton()
    {
        return CheckJumpButtonLegacy.RCast<CCSPlayerLegacyJump_CheckJumpButton_t>();
    }

    CCSPlayerModernJump_CheckJumpButton_t Addresses::CCSPlayerModernJump_CheckJumpButton()
    {
        return CheckJumpButtonModern.RCast<CCSPlayerModernJump_CheckJumpButton_t>();
    }

    CAttributeList_SetOrAddAttributeValueByName_t Addresses::CAttributeList_SetOrAddAttributeValueByName()
    {
        return SetOrAddAttributeValueByName.RCast<CAttributeList_SetOrAddAttributeValueByName_t>();
    }

    CDecoyProjectile_EmitGrenade_t Addresses::CDecoyProjectile_EmitGrenade()
    {
        return EmitDecoy.RCast<CDecoyProjectile_EmitGrenade_t>();
    }

    CFlashbangProjectile_EmitGrenade_t Addresses::CFlashbangProjectile_EmitGrenade()
    {
        return EmitFlashbang.RCast<CFlashbangProjectile_EmitGrenade_t>();
    }

    CHEGrenadeProjectile_EmitGrenade_t Addresses::CHEGrenadeProjectile_EmitGrenade()
    {
        return EmitHEGrenade.RCast<CHEGrenadeProjectile_EmitGrenade_t>();
    }

    CMolotovProjectile_EmitGrenade_t Addresses::CMolotovProjectile_EmitGrenade()
    {
        return EmitMolotov.RCast<CMolotovProjectile_EmitGrenade_t>();
    }

    CSmokeGrenadeProjectile_EmitGrenade_t Addresses::CSmokeGrenadeProjectile_EmitGrenade()
    {
        return EmitSmoke.RCast<CSmokeGrenadeProjectile_EmitGrenade_t>();
    }

    CCSPlayerPawn_PostThink_t Addresses::CCSPlayerPawn_PostThink()
    {
        return PostThink.RCast<CCSPlayerPawn_PostThink_t>();
    }

    UTIL_Remove_t Addresses::UTIL_Remove()
    {
        return UTIL_RemoveAddr.RCast<UTIL_Remove_t>();
    }

    DispatchParticleEffect_t Addresses::DispatchParticleEffect()
    {
        return DispatchParticleEffectAddr.RCast<DispatchParticleEffect_t>();
    }

    GetWeaponCSDataFromKey_t Addresses::GetWeaponCSDataFromKey()
    {
        return GetWeaponCSDataFromKeyAddr.RCast<GetWeaponCSDataFromKey_t>();
    }

    CSource2Server_GetNavMeshData_t Addresses::CSource2Server_GetNavMeshData()
    {
        return GetNavMeshData.RCast<CSource2Server_GetNavMeshData_t>();
    }

    CLoggingSystem_LogDirect_t Addresses::CLoggingSystem_LogDirect()
    {
        return LogDirect.RCast<CLoggingSystem_LogDirect_t>();
    }

    Cmd_ExecuteCommand_t Addresses::Cmd_ExecuteCommand()
    {
        return Cmd_ExecuteCommandAddr.RCast<Cmd_ExecuteCommand_t>();
    }

    CCSPlayer_ItemServices_GiveNamedItem_t Addresses::CCSPlayer_ItemServices_GiveNamedItem()
    {
        return GiveNamedItem.RCast<CCSPlayer_ItemServices_GiveNamedItem_t>();
    }
}
