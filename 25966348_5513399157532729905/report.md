# Gamedata validation

CS2 build **25966348** · Linux manifest `5513399157532729905` · Windows manifest `8766757721818354077` · 2026-10-15T20:14:08+00:00

| | Windows | Linux |
|---|---|---|
| Signatures | 🟢 66 | 🟢 66 |
| Offsets | 🟢 39 ⚪ 4 | 🟢 39 ⚪ 4 |

## Everything resolves


<details><summary>All entries (Windows, Linux)</summary>

- 🟢🟢 `BotNavIgnore1`
- 🟢🟢 `CAttributeList::SetOrAddAttributeValueByName`
- 🟢🟢 `CBaseEntity::CollisionRulesChanged` — W 188/238 · L 187/239
- 🟢🟢 `CBaseEntity::DispatchSpawn`
- 🟢🟢 `CBaseEntity::EndTouch` — W 152/238 · L 151/239
- 🟢🟢 `CBaseEntity::StartTouch` — W 150/238 · L 149/239
- 🟢🟢 `CBaseEntity::TakeDamageOld`
- 🟢🟢 `CBaseEntity::Teleport` — W 165/238 · L 164/239
- 🟢🟢 `CBaseEntity::Touch` — W 151/238 · L 150/239
- 🟢🟢 `CBaseModelEntity::SetModel`
- 🟢🟢 `CBasePlayerController::OnSimulateUserCommands`
- 🟢🟢 `CBasePlayerController::SetPawn`
- 🟢🟢 `CBasePlayerPawn::CommitSuicide` — W 387/400 · L 387/401
- 🟢🟢 `CBasePlayerPawn::SnapViewAngles`
- 🟢🟢 `CCSGameRules::GoToIntermission` — W 129/130 · L 130/131
- 🟢🟢 `CCSGameRules::TerminateRound`
- 🟢🟢 `CCSPlayerController::ChangeTeam` — W 105/277 · L 104/279
- 🟢🟢 `CCSPlayerController::ProcessUserCmd`
- 🟢🟢 `CCSPlayerController::Respawn` — W 274/277 · L 276/279
- 🟢🟢 `CCSPlayerController::SetClan`
- 🟢🟢 `CCSPlayerController::SwitchTeam`
- 🟢🟢 `CCSPlayerLegacyJump::CheckJumpButton`
- 🟢🟢 `CCSPlayerLegacyJump::OnJump`
- 🟢🟢 `CCSPlayerModernJump::CheckJumpButton`
- 🟢🟢 `CCSPlayerModernJump::OnJump`
- 🟢🟢 `CCSPlayerPawn::PostThink`
- 🟢🟢 `CCSPlayerPawnBase::CanMove`
- 🟢🟢 `CCSPlayer_ItemServices::CanAcquire`
- 🟢🟢 `CCSPlayer_ItemServices::DropActivePlayerWeapon` — W 26/28 · L 27/29
- 🟢🟢 `CCSPlayer_ItemServices::GiveNamedItem`
- 🟢🟢 `CCSPlayer_ItemServices::RemoveWeapons` — W 27/28 · L 28/29
- 🟢🟢 `CCSPlayer_MovementServices::AirAccelerate`
- 🟢🟢 `CCSPlayer_MovementServices::AirMove`
- 🟢🟢 `CCSPlayer_MovementServices::CanUnduck`
- 🟢🟢 `CCSPlayer_MovementServices::CategorizePosition`
- 🟢🟢 `CCSPlayer_MovementServices::CheckFalling`
- 🟢🟢 `CCSPlayer_MovementServices::CheckParameters`
- 🟢🟢 `CCSPlayer_MovementServices::CheckVelocity`
- 🟢🟢 `CCSPlayer_MovementServices::CheckWater`
- 🟢🟢 `CCSPlayer_MovementServices::Duck`
- 🟢🟢 `CCSPlayer_MovementServices::Friction`
- 🟢🟢 `CCSPlayer_MovementServices::FullWalkMove`
- 🟢🟢 `CCSPlayer_MovementServices::GroundAccelerate`
- 🟢🟢 `CCSPlayer_MovementServices::LadderMove`
- 🟢🟢 `CCSPlayer_MovementServices::MoveInit`
- 🟢🟢 `CCSPlayer_MovementServices::PlayerMove`
- 🟢🟢 `CCSPlayer_MovementServices::ProcessMovement`
- 🟢🟢 `CCSPlayer_MovementServices::SetupMove`
- 🟢🟢 `CCSPlayer_MovementServices::TryPlayerMove`
- 🟢🟢 `CCSPlayer_MovementServices::WalkMove`
- 🟢🟢 `CCSPlayer_MovementServices::WaterMove`
- 🟢🟢 `CCSPlayer_WeaponServices::BumpWeapon` — W 29/45 · L 30/48
- 🟢🟢 `CCSPlayer_WeaponServices::CanUse` — W 27/45 · L 28/48
- 🟢🟢 `CCSPlayer_WeaponServices::Destroy`
- 🟢🟢 `CCSPlayer_WeaponServices::DropWeapon` — W 28/45 · L 29/48
- 🟢🟢 `CCSPlayer_WeaponServices::HandleDropWeapon`
- 🟢🟢 `CCSPlayer_WeaponServices::SelectItem` — W 30/45 · L 31/48
- ⚪⚪ `CCSPointScriptEntity::ScriptComponent`
- 🟢🟢 `CDecoyProjectile::EmitGrenade`
- 🟢🟢 `CEntityIOOutput::FireOutputInternal`
- 🟢🟢 `CEntityIdentity::AcceptInput`
- 🟢🟢 `CEntityInstance::AcceptInput`
- 🟢🟢 `CEntitySystem::AddEntityIOEvent`
- 🟢🟢 `CFlashbangProjectile::EmitGrenade`
- 🟢🟢 `CGameEntitySystem::FindEntityByClassName`
- 🟢🟢 `CGameEntitySystem::FindEntityByName`
- 🟢🟢 `CGameRules::FindPickerEntity` — W 25/118 · L 26/119
- 🟢🟢 `CGameRules::GetViewVectors` — W 32/118 · L 33/119
- 🟢🟢 `CGameSceneNode::GetSkeletonInstance` — W 12/31 · L 13/32
- 🟢🟢 `CHEGrenadeProjectile::EmitGrenade`
- 🟢🟢 `CLoggingSystem::LogDirect`
- 🟢🟢 `CMolotovProjectile::EmitGrenade`
- ⚪⚪ `CNetworkGameServer::ClientList`
- 🟢🟢 `CPlayer_MovementServices::RunCommand` — W 25/51 · L 26/52
- 🟢🟢 `CPlayer_ObserverServices::IsValidObserverTarget`
- 🟢🟢 `CSScript::RunScript`
- 🟢🟢 `CServerSideClientBase::SendNetMessage` — W 15/79 · L 16/81
- 🟢🟢 `CSmokeGrenadeProjectile::EmitGrenade`
- 🟢🟢 `CSoundSystem::TakeGuid` — W 76/134 · L 75/135
- 🟢🟢 `CSource2Server::GetNavMeshData`
- 🟢🟢 `CTakeDamageInfo::Constructor`
- ⚪⚪ `CTakeDamageInfo::HitGroup`
- 🟢🟢 `Cmd_ExecuteCommand`
- 🟢🟢 `DispatchParticleEffect`
- 🟢🟢 `FilterWorkshopCommands`
- 🟢🟢 `GetWeaponCSDataFromKey`
- 🟢🟢 `ICvar::DispatchConCommand` — W 20/46 · L 20/46
- 🟢🟢 `IGameEventManager2::FireEvent` — W 7/16 · L 8/17
- 🟢🟢 `IGameEventManager2::LoadEventsFromFile` — W 1/16 · L 2/17
- 🟢🟢 `IGameEventSystem::PostEventAbstract` — W 16/21 · L 15/21
- 🟢🟢 `IGameSystem::InitAllSystems->pFirst`
- 🟢🟢 `IGameSystem::OnServerGamePostSimulate` — W 37/65 · L 37/66
- ⚪⚪ `INetworkMessageProcessingPreFilter::FilterMessage`
- 🟢🟢 `INetworkServerService::StartupServer` — W 26/51 · L 27/53
- 🟢🟢 `ISource2GameClients::ClientCommand` — W 17/46 · L 17/46
- 🟢🟢 `ISource2GameClients::ClientConnect` — W 12/46 · L 12/46
- 🟢🟢 `ISource2GameClients::ClientDisconnect` — W 16/46 · L 16/46
- 🟢🟢 `ISource2GameClients::ClientPutInServer` — W 13/46 · L 13/46
- 🟢🟢 `ISource2GameClients::ClientSettingsChanged` — W 19/46 · L 19/46
- 🟢🟢 `ISource2GameClients::ClientSvcUserMessage` — W 38/46 · L 38/46
- 🟢🟢 `ISource2GameClients::ClientVoice` — W 27/46 · L 27/46
- 🟢🟢 `ISource2GameClients::OnClientConnected` — W 11/46 · L 11/46
- 🟢🟢 `ISource2GameEntities::CheckTransmit` — W 12/18 · L 13/21
- 🟢🟢 `ISource2Server::GameFrame` — W 19/103 · L 19/104
- 🟢🟢 `ISource2Server::GameServerSteamAPIActivated` — W 41/103 · L 41/104
- 🟢🟢 `ISource2Server::GameServerSteamAPIDeactivated` — W 42/103 · L 42/104
- 🟢🟢 `LegacyGameEventListener`
- 🟢🟢 `UTIL::CreateEntityByName`
- 🟢🟢 `UTIL::Remove`

</details>

Legend: 🟢 ok · 🟡 ambiguous or vtable size changed · 🔴 broken · ⚪ not checked · ➖ no entry for the platform
