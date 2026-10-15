/**
 * Source2Mod SourcePawn API: game hooks (ISource2ModGameHooks).
 *
 * Game functions the core hooks from its own gamedata. A handler gets the
 * call's context -- valid inside the handler -- and post, and answers an
 * action; for a function with a return value, `result` is what the caller
 * gets when the handler answers Action_Override or Action_Supersede (in post
 * it holds what the original returned). CallOriginal() (pre) runs the game's
 * function now, past every hook, and returns its value; answer
 * Action_Supersede after it, or it runs twice.
 *
 * What the game passes by pointer (the damage info, the move data, a vector)
 * can be changed in place and the original sees the change; what it passes
 * by value (a speed, a flag) is read-only, as in C++.
 *
 * Objects without an entity index (the services, the jump component, the
 * econ item, the user command) are SchemaObjects; CUserCmd has no schema
 * class, its SchemaObject is opaque (for Source2ModFunction calls).
 */

#if defined _source2mod_gamehooks_included
 #endinput
#endif
#define _source2mod_gamehooks_included

#include <source2mod/types>
#include <source2mod/schema>
#include <source2mod/trace>

/** What CCSPlayer_ItemServices::CanAcquire answers. */
enum AcquireResult
{
	AcquireResult_Allowed = 0,
	AcquireResult_InvalidItem,
	AcquireResult_AlreadyOwned,
	AcquireResult_AlreadyPurchased,
	AcquireResult_ReachedGrenadeTypeLimit,
	AcquireResult_ReachedGrenadeTotalLimit,
	AcquireResult_NotAllowedByTeam,
	AcquireResult_NotAllowedByMap,
	AcquireResult_NotAllowedByMode,
	AcquireResult_NotAllowedForPurchase,
	AcquireResult_NotAllowedByProhibition
};

/** How an item is being acquired in CCSPlayer_ItemServices::CanAcquire. */
enum AcquireMethod
{
	AcquireMethod_PickUp = 0,
	AcquireMethod_Buy,
	AcquireMethod_BuyWithCtrl
};

/** The hooks, for Source2ModGameHooks.IsAvailable (C++ GameHook). */
enum Source2ModGameHook
{
	GameHook_TakeDamage = 0,
	GameHook_CanAcquire,
	GameHook_CanMove,
	GameHook_CanUse,
	GameHook_PostThink,
	GameHook_ProcessUsercmds,
	GameHook_SimulateUserCommands,
	GameHook_RunCommand,
	GameHook_AcceptInput,
	GameHook_Touch,
	GameHook_DropWeapon,
	GameHook_AirAccelerate,
	GameHook_AirMove,
	GameHook_CanUnduck,
	GameHook_CategorizePosition,
	GameHook_CheckFalling,
	GameHook_CheckParameters,
	GameHook_CheckVelocity,
	GameHook_CheckWater,
	GameHook_Duck,
	GameHook_Friction,
	GameHook_FullWalkMove,
	GameHook_GroundAccelerate,
	GameHook_LadderMove,
	GameHook_MoveInit,
	GameHook_PlayerMove,
	GameHook_ProcessMovement,
	GameHook_SetupMove,
	GameHook_TryPlayerMove,
	GameHook_WalkMove,
	GameHook_WaterMove,
	GameHook_OnJumpLegacy,
	GameHook_OnJumpModern,
	GameHook_CheckJumpButtonLegacy,
	GameHook_CheckJumpButtonModern
};

/** The damage being dealt (CTakeDamageInfo); valid inside the hook. */
methodmap CTakeDamageInfo
{
	property float m_flDamage
	{
		public native get();
		public native set(float value);
	}
	property float m_flOriginalDamage
	{
		public native get();
	}
	property float m_flTotalledDamage
	{
		public native get();
		public native set(float value);
	}
	property int m_bitsDamageType
	{
		public native get();
		public native set(int value);
	}
	property int m_iDamageCustom
	{
		public native get();
		public native set(int value);
	}
	property int m_iAmmoType
	{
		public native get();
		public native set(int value);
	}
	/** HitGroup_t; also what CTakeDamageInfo::GetHitGroup() answers. */
	property int m_iHitGroupId
	{
		public native get();
		public native set(int value);
	}
	property bool m_bShouldBleed
	{
		public native get();
		public native set(bool value);
	}
	property bool m_bShouldSpark
	{
		public native get();
		public native set(bool value);
	}
	property bool m_bStoppedBullet
	{
		public native get();
		public native set(bool value);
	}
	property int m_nNumObjectsPenetrated
	{
		public native get();
		public native set(int value);
	}
	property float m_flFriendlyFireDamageReductionRatio
	{
		public native get();
		public native set(float value);
	}
	property bool m_bInTakeDamageFlow
	{
		public native get();
	}
	property CBaseEntity m_hInflictor
	{
		public native get();
		public native set(CBaseEntity entity);
	}
	property CBaseEntity m_hAttacker
	{
		public native get();
		public native set(CBaseEntity entity);
	}
	property CBaseEntity m_hAbility
	{
		public native get();
		public native set(CBaseEntity entity);
	}
	/** TakeDamageFlags_t (64-bit) as two cells: flags[0] the low 32 bits. */
	public native void m_nDamageFlags(int flags[2]);
	public native void Set_m_nDamageFlags(const int flags[2]);
	public native void m_vecDamageForce(float vec[3]);
	public native void Set_m_vecDamageForce(const float vec[3]);
	public native void m_vecDamagePosition(float vec[3]);
	public native void Set_m_vecDamagePosition(const float vec[3]);
	public native void m_vecReportedPosition(float vec[3]);
	public native void Set_m_vecReportedPosition(const float vec[3]);
	public native void m_vecDamageDirection(float vec[3]);
	public native void Set_m_vecDamageDirection(const float vec[3]);
	/** HitGroup_t, CTakeDamageInfo::GetHitGroup(). */
	public native int GetHitGroup();
	/** The trace of the hit (m_pTrace), a copy; null when the damage had none. Close it. */
	public native TraceResult GetTrace();
	/**
	 * m_AttackerInfo: whether the attacker is a pawn / the world, its player
	 * slot and team.
	 * @return The attacker's pawn, NULL_ENTITY when none
	 */
	public native CCSPlayerPawn GetAttackerInfo(bool &isPawn, bool &isWorld, int &playerSlot, int &team);
}

/** What TakeDamage did (CTakeDamageResult); valid inside the hook, mainly in post. */
methodmap CTakeDamageResult
{
	property int m_nHealthLost
	{
		public native get();
		public native set(int value);
	}
	property int m_nHealthBefore
	{
		public native get();
	}
	property float m_flDamageDealt
	{
		public native get();
		public native set(float value);
	}
	property float m_flPreModifiedDamage
	{
		public native get();
	}
	property int m_nTotalledHealthLost
	{
		public native get();
		public native set(int value);
	}
	property float m_flTotalledDamageDealt
	{
		public native get();
		public native set(float value);
	}
	property float m_flTotalledPreModifiedDamage
	{
		public native get();
	}
	property float m_flNewDamageAccumulatorValue
	{
		public native get();
	}
	property bool m_bWasDamageSuppressed
	{
		public native get();
		public native set(bool value);
	}
	property bool m_bSuppressFlinch
	{
		public native get();
		public native set(bool value);
	}
	property int m_nOverrideFlinchHitGroup
	{
		public native get();
		public native set(int value);
	}
	/** TakeDamageFlags_t (64-bit) as two cells. */
	public native void m_nDamageFlags(int flags[2]);
	/** The info that caused it (m_pOriginatingInfo); null when unset. */
	public native CTakeDamageInfo GetOriginatingInfo();
}

/**
 * The movement data (CMoveData) a movement function works on; valid inside
 * the hook. Vectors and angles are methods filling a buffer, Set_<name>()
 * writes them.
 */
methodmap CMoveData
{
	/** The pawn moving (m_nPlayerHandle); NULL_ENTITY when unset. */
	property CCSPlayerPawn m_nPlayerHandle
	{
		public native get();
	}
	property float m_flForwardMove
	{
		public native get();
		public native set(float value);
	}
	/** Moving right is negative (flipped compared to CS:GO). */
	property float m_flSideMove
	{
		public native get();
		public native set(float value);
	}
	property float m_flUpMove
	{
		public native get();
		public native set(float value);
	}
	property bool m_bHasSubtickInputs
	{
		public native get();
		public native set(bool value);
	}
	property int m_nTickCount
	{
		public native get();
	}
	property int m_nTargetTick
	{
		public native get();
	}
	property float m_flSubtickStartFraction
	{
		public native get();
		public native set(float value);
	}
	property float m_flSubtickEndFraction
	{
		public native get();
		public native set(float value);
	}
	property float m_flMaxSpeed
	{
		public native get();
		public native set(float value);
	}
	property float m_flClientMaxSpeed
	{
		public native get();
		public native set(float value);
	}
	property float m_flFrictionDecel
	{
		public native get();
		public native set(float value);
	}
	property bool m_bInAir
	{
		public native get();
		public native set(bool value);
	}
	property bool m_bGameCodeMovedPlayer
	{
		public native get();
		public native set(bool value);
	}
	public native bool HasZeroFrametime();
	public native bool IsLateCommand();
	public native void SetHasZeroFrametime(bool value);
	public native void SetIsLateCommand(bool value);

	public native void m_vecAbsViewAngles(float vec[3]);
	public native void Set_m_vecAbsViewAngles(const float vec[3]);
	public native void m_vecViewAngles(float vec[3]);
	public native void Set_m_vecViewAngles(const float vec[3]);
	public native void m_vecLastMovementImpulses(float vec[3]);
	public native void Set_m_vecLastMovementImpulses(const float vec[3]);
	public native void m_vecVelocity(float vec[3]);
	public native void Set_m_vecVelocity(const float vec[3]);
	public native void m_vecAngles(float vec[3]);
	public native void Set_m_vecAngles(const float vec[3]);
	public native void m_vecCollisionNormal(float vec[3]);
	public native void Set_m_vecCollisionNormal(const float vec[3]);
	public native void m_vecGroundNormal(float vec[3]);
	public native void Set_m_vecGroundNormal(const float vec[3]);
	public native void m_vecAbsOrigin(float vec[3]);
	public native void Set_m_vecAbsOrigin(const float vec[3]);
	public native void m_vecOutWishVel(float vec[3]);
	public native void Set_m_vecOutWishVel(const float vec[3]);
	public native void m_vecOldAngles(float vec[3]);
	public native void Set_m_vecOldAngles(const float vec[3]);
	public native void m_vecAcceleration(float vec[3]);
	public native void Set_m_vecAcceleration(const float vec[3]);
	public native void m_vecContinuousAcceleration(float vec[3]);
	public native void Set_m_vecContinuousAcceleration(const float vec[3]);
	/** Vector2D: vec[2] is set to 0. */
	public native void m_vecWalkWishedVelocity(float vec[3]);
	public native void Set_m_vecWalkWishedVelocity(const float vec[3]);

	/** m_TouchList: what the move touched. */
	public native int GetTouchCount();
	/** One touch: its velocity change, and a copy of its trace (close it). */
	public native TraceResult GetTouch(int index, float deltaVelocity[3]);

	/** m_SubtickMoves (or m_AttackSubtickMoves with attack = true): how many. */
	public native int GetSubtickMoveCount(bool attack = false);
	/**
	 * One subtick move: its button mask (two cells, low 32 bits first) and,
	 * for an analog move (button 0), the deltas forward, left, pitch, yaw.
	 * @return when
	 */
	public native float GetSubtickMove(int index, int button[2], bool &pressed, float analog[4], bool attack = false);
}

/** CBaseEntity::TakeDamageOld(info, result) -> int64 */
methodmap TakeDamageContext
{
	property CBaseEntity entity
	{
		public native get();
	}
	property CTakeDamageInfo info
	{
		public native get();
	}
	/** The damage result being filled; null when the game passed none. */
	property CTakeDamageResult damageResult
	{
		public native get();
	}
	/** Runs the game's TakeDamage now (pre only); returns its result. */
	public native int CallOriginal();
}

/** CCSPlayer_ItemServices::CanAcquire(item, method, unk) -> AcquireResult */
methodmap CanAcquireContext
{
	property CCSPlayer_ItemServices services
	{
		public native get();
	}
	/** The pawn the services belong to. */
	property CCSPlayerPawn pawn
	{
		public native get();
	}
	property CEconItemView item
	{
		public native get();
	}
	property AcquireMethod method
	{
		public native get();
	}
	public native AcquireResult CallOriginal();
}

/** CCSPlayerPawnBase::CanMove() -> bool; false while frozen, defusing, ... */
methodmap CanMoveContext
{
	property CCSPlayerPawnBase pawn
	{
		public native get();
	}
	public native bool CallOriginal();
}

/** CCSPlayer_WeaponServices::CanUse(weapon) -> bool */
methodmap CanUseContext
{
	property CCSPlayer_WeaponServices services
	{
		public native get();
	}
	property CCSPlayerPawn pawn
	{
		public native get();
	}
	property CBasePlayerWeapon weapon
	{
		public native get();
	}
	public native bool CallOriginal();
}

/** CCSPlayerPawn::PostThink() */
methodmap PostThinkContext
{
	property CCSPlayerPawnBase pawn
	{
		public native get();
	}
	public native void CallOriginal();
}

/**
 * CCSPlayerController::ProcessUserCmd(cmds, count, paused, margin) -> void*.
 * The commands are CUserCmd (protobuf-backed, no schema class): opaque.
 */
methodmap ProcessUsercmdsContext
{
	property CCSPlayerController controller
	{
		public native get();
	}
	/** The command array, opaque. */
	property SchemaObject cmds
	{
		public native get();
	}
	property int count
	{
		public native get();
	}
	property bool paused
	{
		public native get();
	}
	property float margin
	{
		public native get();
	}
	/** Runs the original; what it returns is opaque, so only whether it was null. */
	public native bool CallOriginal();
}

/** CBasePlayerController::OnSimulateUserCommands() */
methodmap SimulateUserCommandsContext
{
	property CBasePlayerController controller
	{
		public native get();
	}
	public native void CallOriginal();
}

/** CPlayer_MovementServices::RunCommand(cmd); the command is opaque (CUserCmd). */
methodmap RunCommandContext
{
	property CCSPlayer_MovementServices services
	{
		public native get();
	}
	property CCSPlayerPawn pawn
	{
		public native get();
	}
	property SchemaObject cmd
	{
		public native get();
	}
	public native void CallOriginal();
}

/** CEntityIdentity::AcceptInput(name, activator, caller, value, ...) -> bool */
methodmap AcceptInputContext
{
	/** The entity receiving the input. */
	property CEntityInstance entity
	{
		public native get();
	}
	property CEntityInstance activator
	{
		public native get();
	}
	property CEntityInstance caller
	{
		public native get();
	}
	public native int GetInputName(char[] buffer, int maxlen);
	/** The value (variant_t) as text; "" when there is none. */
	public native int GetValue(char[] buffer, int maxlen);
	/** Replaces the value with a string; the original (and later handlers) see it. */
	public native bool SetValue(const char[] value);
	public native bool CallOriginal();
}

/** CBaseEntity::Touch(other), on CBaseEntity's own vtable. */
methodmap TouchContext
{
	property CBaseEntity entity
	{
		public native get();
	}
	property CBaseEntity other
	{
		public native get();
	}
	public native void CallOriginal();
}

/** CCSPlayer_WeaponServices::DropWeapon(weapon, target, velocity) */
methodmap DropWeaponContext
{
	property CCSPlayer_WeaponServices services
	{
		public native get();
	}
	property CCSPlayerPawn pawn
	{
		public native get();
	}
	property CBasePlayerWeapon weapon
	{
		public native get();
	}
	/** False when the game passed no target / velocity (the getters then leave the buffer). */
	public native bool GetTarget(float vec[3]);
	public native bool SetTarget(const float vec[3]);
	public native bool GetVelocity(float vec[3]);
	public native bool SetVelocity(const float vec[3]);
	public native void CallOriginal();
}

/**
 * The CCSPlayer_MovementServices functions that take the move data alone:
 * AirMove, CheckFalling, CheckParameters, Duck, Friction, PlayerMove,
 * ProcessMovement, WalkMove, WaterMove (void: `result` is unused), and
 * CanUnduck, CheckWater, LadderMove, MoveInit (bool).
 */
methodmap MovementContext
{
	property CCSPlayer_MovementServices services
	{
		public native get();
	}
	property CCSPlayerPawn pawn
	{
		public native get();
	}
	property CMoveData moveData
	{
		public native get();
	}
	/** The void functions answer false. */
	public native bool CallOriginal();
}

/** CCSPlayer_MovementServices::AirAccelerate(move, wishDirection, wishSpeed, acceleration) */
methodmap AirAccelerateContext
{
	property CCSPlayer_MovementServices services
	{
		public native get();
	}
	property CCSPlayerPawn pawn
	{
		public native get();
	}
	property CMoveData moveData
	{
		public native get();
	}
	property float wishSpeed
	{
		public native get();
	}
	property float acceleration
	{
		public native get();
	}
	public native void GetWishDirection(float vec[3]);
	public native void SetWishDirection(const float vec[3]);
	public native void CallOriginal();
}

/** CCSPlayer_MovementServices::GroundAccelerate(move, wishDirection, frameTime, wishSpeed, acceleration) */
methodmap GroundAccelerateContext
{
	property CCSPlayer_MovementServices services
	{
		public native get();
	}
	property CCSPlayerPawn pawn
	{
		public native get();
	}
	property CMoveData moveData
	{
		public native get();
	}
	property float frameTime
	{
		public native get();
	}
	property float wishSpeed
	{
		public native get();
	}
	property float acceleration
	{
		public native get();
	}
	public native void GetWishDirection(float vec[3]);
	public native void SetWishDirection(const float vec[3]);
	public native void CallOriginal();
}

/** CCSPlayer_MovementServices::CategorizePosition(move, stayOnGround) */
methodmap CategorizePositionContext
{
	property CCSPlayer_MovementServices services
	{
		public native get();
	}
	property CCSPlayerPawn pawn
	{
		public native get();
	}
	property CMoveData moveData
	{
		public native get();
	}
	property bool stayOnGround
	{
		public native get();
	}
	public native void CallOriginal();
}

/** CCSPlayer_MovementServices::CheckVelocity(move, unk); the second argument is unknown and not exposed. */
methodmap CheckVelocityContext
{
	property CCSPlayer_MovementServices services
	{
		public native get();
	}
	property CCSPlayerPawn pawn
	{
		public native get();
	}
	property CMoveData moveData
	{
		public native get();
	}
	public native void CallOriginal();
}

/** CCSPlayer_MovementServices::FullWalkMove(move, onGround) */
methodmap FullWalkMoveContext
{
	property CCSPlayer_MovementServices services
	{
		public native get();
	}
	property CCSPlayerPawn pawn
	{
		public native get();
	}
	property CMoveData moveData
	{
		public native get();
	}
	property bool onGround
	{
		public native get();
	}
	public native void CallOriginal();
}

/** CCSPlayer_MovementServices::SetupMove(cmd, move); the command is opaque (CUserCmd). */
methodmap SetupMoveContext
{
	property CCSPlayer_MovementServices services
	{
		public native get();
	}
	property CCSPlayerPawn pawn
	{
		public native get();
	}
	property SchemaObject cmd
	{
		public native get();
	}
	property CMoveData moveData
	{
		public native get();
	}
	public native void CallOriginal();
}

/** CCSPlayer_MovementServices::TryPlayerMove(move, firstDest, firstTrace, isSurfing) */
methodmap TryPlayerMoveContext
{
	property CCSPlayer_MovementServices services
	{
		public native get();
	}
	property CCSPlayerPawn pawn
	{
		public native get();
	}
	property CMoveData moveData
	{
		public native get();
	}
	/** False when the game passed no first destination. */
	public native bool GetFirstDest(float vec[3]);
	public native bool SetFirstDest(const float vec[3]);
	/** A copy of the first trace; null when the game passed none. Close it. */
	public native TraceResult GetFirstTrace();
	/** *isSurfing; false when the game passed no pointer. */
	public native bool GetIsSurfing();
	public native bool SetIsSurfing(bool value);
	public native void CallOriginal();
}

/** CCSPlayerLegacyJump::OnJump / CheckJumpButton(move) */
methodmap LegacyJumpContext
{
	property CCSPlayerLegacyJump jump
	{
		public native get();
	}
	property CMoveData moveData
	{
		public native get();
	}
	public native void CallOriginal();
}

/** CCSPlayerModernJump::OnJump / CheckJumpButton(move) */
methodmap ModernJumpContext
{
	property CCSPlayerModernJump jump
	{
		public native get();
	}
	property CMoveData moveData
	{
		public native get();
	}
	public native void CallOriginal();
}

typedef Source2ModTakeDamageHandler = function Source2ModAction (TakeDamageContext ctx, bool post, int &result);
typedef Source2ModCanAcquireHandler = function Source2ModAction (CanAcquireContext ctx, bool post, AcquireResult &result);
typedef Source2ModCanMoveHandler = function Source2ModAction (CanMoveContext ctx, bool post, bool &result);
typedef Source2ModCanUseHandler = function Source2ModAction (CanUseContext ctx, bool post, bool &result);
typedef Source2ModPostThinkHandler = function Source2ModAction (PostThinkContext ctx, bool post);
/** ProcessUserCmd's return value is opaque: Override / Supersede keep ctx.result (null in pre, the original's in post). */
typedef Source2ModProcessUsercmdsHandler = function Source2ModAction (ProcessUsercmdsContext ctx, bool post);
typedef Source2ModSimulateUserCommandsHandler = function Source2ModAction (SimulateUserCommandsContext ctx, bool post);
typedef Source2ModRunCommandHandler = function Source2ModAction (RunCommandContext ctx, bool post);
typedef Source2ModAcceptInputHandler = function Source2ModAction (AcceptInputContext ctx, bool post, bool &result);
typedef Source2ModTouchHandler = function Source2ModAction (TouchContext ctx, bool post);
typedef Source2ModDropWeaponHandler = function Source2ModAction (DropWeaponContext ctx, bool post);
/** `result` matters for CanUnduck, CheckWater, LadderMove and MoveInit only. */
typedef Source2ModMovementHandler = function Source2ModAction (MovementContext ctx, bool post, bool &result);
typedef Source2ModAirAccelerateHandler = function Source2ModAction (AirAccelerateContext ctx, bool post);
typedef Source2ModGroundAccelerateHandler = function Source2ModAction (GroundAccelerateContext ctx, bool post);
typedef Source2ModCategorizePositionHandler = function Source2ModAction (CategorizePositionContext ctx, bool post);
typedef Source2ModCheckVelocityHandler = function Source2ModAction (CheckVelocityContext ctx, bool post);
typedef Source2ModFullWalkMoveHandler = function Source2ModAction (FullWalkMoveContext ctx, bool post);
typedef Source2ModSetupMoveHandler = function Source2ModAction (SetupMoveContext ctx, bool post);
typedef Source2ModTryPlayerMoveHandler = function Source2ModAction (TryPlayerMoveContext ctx, bool post);
typedef Source2ModLegacyJumpHandler = function Source2ModAction (LegacyJumpContext ctx, bool post);
typedef Source2ModModernJumpHandler = function Source2ModAction (ModernJumpContext ctx, bool post);

/**
 * Every HookX returns the id UnhookX takes (0 when it failed). The movement
 * and think hooks run for every player every tick: do as little as possible.
 */
methodmap Source2ModGameHooks
{
	public static native int HookTakeDamage(Source2ModTakeDamageHandler handler, bool post);
	public static native bool UnhookTakeDamage(int id);
	public static native int HookCanAcquire(Source2ModCanAcquireHandler handler, bool post);
	public static native bool UnhookCanAcquire(int id);
	public static native int HookCanMove(Source2ModCanMoveHandler handler, bool post);
	public static native bool UnhookCanMove(int id);
	public static native int HookCanUse(Source2ModCanUseHandler handler, bool post);
	public static native bool UnhookCanUse(int id);
	public static native int HookPostThink(Source2ModPostThinkHandler handler, bool post);
	public static native bool UnhookPostThink(int id);
	public static native int HookProcessUsercmds(Source2ModProcessUsercmdsHandler handler, bool post);
	public static native bool UnhookProcessUsercmds(int id);
	public static native int HookSimulateUserCommands(Source2ModSimulateUserCommandsHandler handler, bool post);
	public static native bool UnhookSimulateUserCommands(int id);
	public static native int HookRunCommand(Source2ModRunCommandHandler handler, bool post);
	public static native bool UnhookRunCommand(int id);
	public static native int HookAcceptInput(Source2ModAcceptInputHandler handler, bool post);
	public static native bool UnhookAcceptInput(int id);
	public static native int HookTouch(Source2ModTouchHandler handler, bool post);
	public static native bool UnhookTouch(int id);
	public static native int HookDropWeapon(Source2ModDropWeaponHandler handler, bool post);
	public static native bool UnhookDropWeapon(int id);
	public static native int HookAirAccelerate(Source2ModAirAccelerateHandler handler, bool post);
	public static native bool UnhookAirAccelerate(int id);
	public static native int HookAirMove(Source2ModMovementHandler handler, bool post);
	public static native bool UnhookAirMove(int id);
	public static native int HookCanUnduck(Source2ModMovementHandler handler, bool post);
	public static native bool UnhookCanUnduck(int id);
	public static native int HookCategorizePosition(Source2ModCategorizePositionHandler handler, bool post);
	public static native bool UnhookCategorizePosition(int id);
	public static native int HookCheckFalling(Source2ModMovementHandler handler, bool post);
	public static native bool UnhookCheckFalling(int id);
	public static native int HookCheckParameters(Source2ModMovementHandler handler, bool post);
	public static native bool UnhookCheckParameters(int id);
	public static native int HookCheckVelocity(Source2ModCheckVelocityHandler handler, bool post);
	public static native bool UnhookCheckVelocity(int id);
	public static native int HookCheckWater(Source2ModMovementHandler handler, bool post);
	public static native bool UnhookCheckWater(int id);
	public static native int HookDuck(Source2ModMovementHandler handler, bool post);
	public static native bool UnhookDuck(int id);
	public static native int HookFriction(Source2ModMovementHandler handler, bool post);
	public static native bool UnhookFriction(int id);
	public static native int HookFullWalkMove(Source2ModFullWalkMoveHandler handler, bool post);
	public static native bool UnhookFullWalkMove(int id);
	public static native int HookGroundAccelerate(Source2ModGroundAccelerateHandler handler, bool post);
	public static native bool UnhookGroundAccelerate(int id);
	public static native int HookLadderMove(Source2ModMovementHandler handler, bool post);
	public static native bool UnhookLadderMove(int id);
	public static native int HookMoveInit(Source2ModMovementHandler handler, bool post);
	public static native bool UnhookMoveInit(int id);
	public static native int HookPlayerMove(Source2ModMovementHandler handler, bool post);
	public static native bool UnhookPlayerMove(int id);
	public static native int HookProcessMovement(Source2ModMovementHandler handler, bool post);
	public static native bool UnhookProcessMovement(int id);
	public static native int HookSetupMove(Source2ModSetupMoveHandler handler, bool post);
	public static native bool UnhookSetupMove(int id);
	public static native int HookTryPlayerMove(Source2ModTryPlayerMoveHandler handler, bool post);
	public static native bool UnhookTryPlayerMove(int id);
	public static native int HookWalkMove(Source2ModMovementHandler handler, bool post);
	public static native bool UnhookWalkMove(int id);
	public static native int HookWaterMove(Source2ModMovementHandler handler, bool post);
	public static native bool UnhookWaterMove(int id);
	public static native int HookOnJumpLegacy(Source2ModLegacyJumpHandler handler, bool post);
	public static native bool UnhookOnJumpLegacy(int id);
	public static native int HookOnJumpModern(Source2ModModernJumpHandler handler, bool post);
	public static native bool UnhookOnJumpModern(int id);
	public static native int HookCheckJumpButtonLegacy(Source2ModLegacyJumpHandler handler, bool post);
	public static native bool UnhookCheckJumpButtonLegacy(int id);
	public static native int HookCheckJumpButtonModern(Source2ModModernJumpHandler handler, bool post);
	public static native bool UnhookCheckJumpButtonModern(int id);

	/**
	 * False when the core's gamedata has no entry for the function on this
	 * platform -- handlers can still be registered, they just never run.
	 */
	public static native bool IsAvailable(Source2ModGameHook hook);
}
