@CBaseEntity
	// CBaseEntity's helpers, as in the SDK's CBaseEntity.h. `chain` is C++'s
	// HookChain: HookChain_Default keeps the C++ default.
	// The entity now at an index, NULL_ENTITY when none: an index into an entity.
	public static native CBaseEntity FromIndex(int index);
	public static native CBaseEntity CreateEntityByName(const char[] className);
	public native void AcceptInput(const char[] input, CEntityInstance activator = NULL_ENTITY, CEntityInstance caller = NULL_ENTITY, const char[] value = "", Source2ModHookChain chain = HookChain_Default);
	public native void AddEntityIOEvent(const char[] input, CEntityInstance activator = NULL_ENTITY, CEntityInstance caller = NULL_ENTITY, const char[] value = "", float delay = 0.0, Source2ModHookChain chain = HookChain_Default);
	// AddSingleEntityIOListener: an output of this entity only; the id goes to Source2ModEntities.RemoveEntityIOListener.
	public native int AddSingleEntityIOListener(const char[] output, Source2ModEntityOutputHandler handler, bool post = false);
	public native void GetAbsOrigin(float origin[3]);
	public native void GetAngRotation(float angles[3]);
	public native void GetAbsRotation(float angles[3]);
	public native void GetAbsVelocity(float velocity[3]);
	public native void SetAbsOrigin(const float origin[3]);
	public native void SetAbsRotation(const float angles[3]);
	public native void SetAngRotation(const float angles[3]);
	public native void SetAbsVelocity(const float velocity[3]);
	public native void SetBaseVelocity(const float velocity[3]);
	public native CEntitySubclassVDataBase GetVData();
	// keyValues: optional StringMap of key -> string value, applied as the entity's spawn keyvalues.
	public native void DispatchSpawn(StringMap keyValues = null, Source2ModHookChain chain = HookChain_Default);
	public native void Teleport(const float position[3] = NULL_VECTOR, const float angles[3] = NULL_VECTOR, const float velocity[3] = NULL_VECTOR, Source2ModHookChain chain = HookChain_Default);
	// MoveType_t.
	public native void SetMoveType(int moveType);
	public native int GetCollisionGroup();
	public native void SetCollisionGroup(int collisionGroup);
	public native void CollisionRulesChanged();
	public native void Remove(Source2ModHookChain chain = HookChain_Default);
	// The entity index (GetEntityIndex): an entity into an index.
	public native int GetIndex();
	// ParticleAttachment_t (PATTACH_ABSORIGIN_FOLLOW = 1); recipients: a player mask, all players by default.
	public native void DispatchParticleEffect(const char[] particle, int attachType = 1, int attachmentPoint = 0, bool resetAllParticles = false, const int recipients[2] = {-1, -1}, Source2ModHookChain chain = HookChain_Default);
@CBaseModelEntity
	public native int GetModelName(char[] buffer, int maxlen);
	public native void GetEyePosition(float position[3]);
	public native void SetModel(const char[] model, Source2ModHookChain chain = HookChain_Default);
	public native void SetBodyGroup(const char[] name, int value);
@CBasePlayerController
	public native void SetPawn(CBasePlayerPawn pawn, Source2ModHookChain chain = HookChain_Default);
	public native CCSPlayerPawn GetPawn();
	public native void PrintToConsole(const char[] message);
	public native void PrintToChat(const char[] message);
	public native void PrintToCenter(const char[] message);
	public native void PrintToCenterAlert(const char[] message);
	public native bool IsBot();
	public native void Disconnect(int reason, const char[] internalReason = "");
	public native void ExecuteClientCommand(const char[] command);
	public native void ExecuteClientCommandFromServer(const char[] command);
	public native CPlayerSlot GetPlayerSlot();
	public native int GetSlot();
	public native int GetUserID();
	// The 64-bit SteamID as decimal text.
	public native void GetSteamID(char[] buffer, int maxlen);
	public native int GetPlayerName(char[] buffer, int maxlen);
	public native void GetIpAddress(char[] buffer, int maxlen);
	public native void ReplicateConVar(const char[] name, const char[] value);
	public native void FireEventToClient(IGameEvent event);
@CCSPlayerController
	// The controller in a player slot, NULL_ENTITY when the slot is empty.
	public static native CCSPlayerController FromSlot(CPlayerSlot slot);
	public static native CCSPlayerController FromPawn(CCSPlayerPawn pawn);
	public static native CCSPlayerController FromUserId(int userId);
	// The 64-bit SteamID as decimal text.
	public static native CCSPlayerController FromSteamId(const char[] steamId);
	public native void PrintToCenterHtml(const char[] message, int duration = 5, bool menu = false);
	// DamageTypes_t; C++ defaults to HookChain_Bypass here. The victim must be alive and the attacker a
	// connected player with a pawn (it may be the victim itself): otherwise the C++ helper does nothing.
	public native void TakeDamage(CCSPlayerController attacker, int damage, int damageType, Source2ModHookChain chain = HookChain_Default);
	public native void Respawn(Source2ModHookChain chain = HookChain_Default);
	public native void SwitchTeam(int team, Source2ModHookChain chain = HookChain_Default);
	public native void ChangeTeam(int team, Source2ModHookChain chain = HookChain_Default);
	public native CCSPlayerPawn GetPlayerPawn();
	public native CCSObserverPawn GetObserverPawn();
@CBasePlayerPawn
	public native void CommitSuicide(bool explode, bool force, Source2ModHookChain chain = HookChain_Default);
	public native void SnapViewAngles(const float angles[3], Source2ModHookChain chain = HookChain_Default);
	public native CCSPlayerController GetController();
	public native CCSPlayerController GetDefaultController();
@CCSPlayerPawnBase
	public native CCSPlayerController GetOriginalController();
	public native bool CanMove();
@CCSPlayerPawn
	public native void GetEyeAngles(float angles[3]);
	public native void PostThink(Source2ModHookChain chain = HookChain_Default);
@CBasePlayerWeapon
	public native CCSWeaponBaseVData GetWeaponVData();
	public native int GetWeaponClassname(char[] buffer, int maxlen);
@CCSWeaponBaseVData
	// The weapon's VData by its name ("weapon_ak47") or item definition index as text.
	public static native CCSWeaponBaseVData FromKey(const char[] key, int weaponType = -1);
@CPlayerPawnComponent
	public native CCSPlayerPawn GetPlayerPawn();
@CPlayerControllerComponent
	public native CCSPlayerController GetPlayerController();
@CPlayer_ItemServices
	public native void DropActivePlayerWeapon(CBasePlayerWeapon weapon, Source2ModHookChain chain = HookChain_Default);
	public native void RemoveWeapons(bool removeSuit, Source2ModHookChain chain = HookChain_Default);
	public native CBasePlayerWeapon GiveNamedItem(const char[] item, Source2ModHookChain chain = HookChain_Default);
@CPlayer_WeaponServices
	public native bool CanUse(CBasePlayerWeapon weapon);
	public native void DropWeapon(CBasePlayerWeapon weapon, const float target[3] = NULL_VECTOR, const float velocity[3] = NULL_VECTOR, Source2ModHookChain chain = HookChain_Default);
	public native int BumpWeapon(CBasePlayerWeapon weapon, Source2ModHookChain chain = HookChain_Default);
	public native void SelectItem(CBasePlayerWeapon weapon, Source2ModHookChain chain = HookChain_Default);
	public native void Destroy(CBasePlayerWeapon weapon, Source2ModHookChain chain = HookChain_Default);
@CAttributeList
	public native void SetOrAddAttribute(const char[] name, float value, Source2ModHookChain chain = HookChain_Default);
	public native void SetOrAddAttributeValueByName(const char[] name, float value, Source2ModHookChain chain = HookChain_Default);
@CGameSceneNode
	public native CSkeletonInstance GetSkeletonInstance();
@CDecoyProjectile
	public static native CDecoyProjectile EmitGrenade(const float position[3], const float angles[3], const float velocity[3], CBaseEntity owner, int itemDefIndex, Source2ModHookChain chain = HookChain_Default);
@CFlashbangProjectile
	public static native CFlashbangProjectile EmitGrenade(const float position[3], const float angles[3], const float velocity[3], CBaseEntity owner, int itemDefIndex, Source2ModHookChain chain = HookChain_Default);
@CHEGrenadeProjectile
	public static native CHEGrenadeProjectile EmitGrenade(const float position[3], const float angles[3], const float velocity[3], CBaseEntity owner, int itemDefIndex, Source2ModHookChain chain = HookChain_Default);
@CMolotovProjectile
	public static native CMolotovProjectile EmitGrenade(const float position[3], const float angles[3], const float velocity[3], CBaseEntity owner, int itemDefIndex, Source2ModHookChain chain = HookChain_Default);
@CSmokeGrenadeProjectile
	public static native CSmokeGrenadeProjectile EmitGrenade(const float position[3], const float angles[3], const float velocity[3], int team, CBaseEntity owner, int itemDefIndex, Source2ModHookChain chain = HookChain_Default);
@CCSGameRules
	public native void GoToIntermission(bool abortedMatch = false, Source2ModHookChain chain = HookChain_Default);
	// C++ defaults to HookChain_Bypass here.
	public native void TerminateRound(float delay, int reason, int teamId = 0, Source2ModHookChain chain = HookChain_Default);
	public native CBaseEntity FindPickerEntity(CBasePlayerController player);
	public native CCSPlayerController GetClientAimTarget(CCSPlayerController player);
@CCSCustomHudLayout
	// NULL_ENTITY as the controller: every player (the layout's shared state).
	public static native CCSCustomHudLayout Create(const char[] layout, const char[] targetName = "");
	public native CCSCustomHudLayoutState GetLayoutState(CCSPlayerController controller = NULL_ENTITY);
	public native void SetHasClass(const char[] panelId, const char[] className, bool hasClass, CCSPlayerController controller = NULL_ENTITY);
	public native void SetDialogVariableString(const char[] panelId, const char[] variable, const char[] value, CCSPlayerController controller = NULL_ENTITY);
	public native void SetInputCaptureEnabled(bool enable, CCSPlayerController controller = NULL_ENTITY);
	public native bool IsInputCaptureEnabled(CCSPlayerController controller = NULL_ENTITY);
