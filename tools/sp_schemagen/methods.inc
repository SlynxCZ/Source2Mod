@CBaseEntity
	// CBaseEntity's helpers, as in the SDK's CBaseEntity.h.
	public static native CBaseEntity FromIndex(int index);
	public static native CBaseEntity CreateEntityByName(const char[] className);
	public native void AcceptInput(const char[] input, CEntityInstance activator = NULL_ENTITY, CEntityInstance caller = NULL_ENTITY, const char[] value = "");
	public native void GetAbsOrigin(float origin[3]);
	public native void DispatchSpawn();
	public native void Teleport(const float position[3] = NULL_VECTOR, const float angles[3] = NULL_VECTOR, const float velocity[3] = NULL_VECTOR);
	public native void Remove();
	public native int GetIndex();
@CBaseModelEntity
	public native int GetModelName(char[] buffer, int maxlen);
	public native void GetEyePosition(float position[3]);
	public native void SetModel(const char[] model);
	public native void SetBodyGroup(const char[] name, int value);
@CBasePlayerController
	public native void SetPawn(CBasePlayerPawn pawn);
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
	public static native CCSPlayerController FromSlot(int slot);
	public native void PrintToCenterHtml(const char[] message, int duration = 5, bool menu = false);
	public native void Respawn();
	public native void SwitchTeam(int team);
	public native void ChangeTeam(int team);
	public native CCSPlayerPawn GetPlayerPawn();
	public native CCSObserverPawn GetObserverPawn();
@CBasePlayerPawn
	public native void CommitSuicide(bool explode, bool force);
	public native void SnapViewAngles(const float angles[3]);
	public native CCSPlayerController GetController();
@CCSPlayerPawn
	public native void GetEyeAngles(float angles[3]);
@CPlayer_ItemServices
	public native void DropActivePlayerWeapon(CBasePlayerWeapon weapon);
	public native void RemoveWeapons(bool removeSuit);
	public native CBasePlayerWeapon GiveNamedItem(const char[] item);
@CPlayer_WeaponServices
	public native bool CanUse(CBasePlayerWeapon weapon);
	public native void DropWeapon(CBasePlayerWeapon weapon, const float target[3] = NULL_VECTOR, const float velocity[3] = NULL_VECTOR);
	public native int BumpWeapon(CBasePlayerWeapon weapon);
	public native void SelectItem(CBasePlayerWeapon weapon);
@CCSGameRules
	public native void TerminateRound(float delay, int reason, int teamId = 0);
