/**
 * vim: set ts=4 sw=4 tw=99 noet :
 * ======================================================
 * Source2Mod SourcePawn Sample Plugin
 * ======================================================
 *
 * cpp_sample, written in SourcePawn: the same twelve sections, numbered the
 * same, doing the same things through the script API -- which mirrors the
 * C++ one, interface for interface (ISource2ModCommands is
 * Source2ModCommands, a schema class is a methodmap of the same name, a
 * field is a property of the same name):
 *
 *   1. Lifecycle        OnPluginStart / OnPluginEnd / the listener forwards
 *   2. ConVars          CConVar, CConVarRef, change hook
 *   3. Commands         console + chat commands, arguments, replies, listeners
 *   4. Game events      pre / post hooks, dontBroadcast, firing an event
 *   5. Core events      entity listener, entity outputs, client callbacks,
 *                       GameFrame; a raw hook where Source2Mod has nothing
 *   6. Net messages     building + sending, outgoing and incoming hooks
 *   7. Sounds           one-call emit, sound objects, stopping, the sound hook
 *   8. Game functions   game hooks (TakeDamage, PostThink), calling by gamedata
 *   9. Entities         schema fields, teleport, items, inputs
 *  10. Timers           next frame, delayed, repeating
 *  11. Transmit         hiding players and entities per viewer, CheckTransmit hook
 *  12. Script natives   using cpp_sample's natives, library and handle type
 *
 * The commands are cpp_sample's with "sp_" in place of "sample_", the convars
 * "sp_sample_..." -- both plugins can be loaded side by side.
 *
 * Where the two read differently it is the language, not the API: no
 * lambdas (handlers are named functions, state lives in globals), no
 * operator overloading on vectors (float[3] and a loop), strings into
 * buffers, and a slot mask is int[2] instead of a uint64.
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from
 * the use of this software.
 *
 * This sample plugin is public domain.
 */

#include <source2mod/api>

// cpp_sample's natives (section 12) -- optional: this plugin loads without it.
#undef REQUIRE_PLUGIN
#include <cpp_sample>

// Generated from plugin-metadata.json by tools/version_gen.py, as cpp_sample's
// version_gen.h is -- the metadata below is not written twice.
#include "version_gen"

#pragma semicolon 1
#pragma newdecls required

public Plugin myinfo =
{
	name = PLUGIN_DISPLAY_NAME,
	author = PLUGIN_AUTHOR,
	description = PLUGIN_DESCRIPTION,
	version = PLUGIN_FULL_VERSION,
	url = PLUGIN_URL
};

/* ============================================================================
 *
 *   Shared helpers
 *
 * ========================================================================== */

// Who ran the command: a controller for a player, NULL_ENTITY for the server
// console -- the console has no slot.
CCSPlayerController CallerOf(Source2ModCommandContext context)
{
	CPlayerSlot slot = context.GetPlayerSlot();
	return slot.IsValid() ? CCSPlayerController.FromSlot(slot.Get()) : NULL_ENTITY;
}

// Answers where the command came from: the player's chat, or the server console.
void Reply(Source2ModCommandContext context, const char[] format, any ...)
{
	char buffer[512];
	VFormat(buffer, sizeof(buffer), format, 3);

	CCSPlayerController caller = CallerOf(context);
	if (caller != NULL_ENTITY)
		caller.PrintToChat(buffer);
	else
		PrintToServer("%s", buffer);
}

// Most of the entity commands need a living caller. Says why when there is none.
CCSPlayerPawn AlivePawnOf(Source2ModCommandContext context)
{
	CCSPlayerController caller = CallerOf(context);
	if (caller == NULL_ENTITY)
	{
		Reply(context, "This command is for players, not for the server console.");
		return NULL_ENTITY;
	}

	CCSPlayerPawn pawn = caller.GetPlayerPawn();
	if (pawn == NULL_ENTITY || !caller.m_bPawnIsAlive)
	{
		Reply(context, "You have to be alive for this.");
		return NULL_ENTITY;
	}

	return pawn;
}

// CCSPlayerController.FromSlot() for every slot that has one; ForEachPlayer()
// in cpp_sample takes a lambda, a script loops.
CCSPlayerController PlayerInSlot(int slot)
{
	return CCSPlayerController.FromSlot(slot);
}

// The first argument as text, "" when there is none.
void Arg1(Source2ModCommandArgs args, char[] buffer, int maxlen)
{
	buffer[0] = '\0';
	if (args.ArgC() >= 2)
		args.Arg(1, buffer, maxlen);
}

/* ============================================================================
 *
 *   1. Lifecycle
 *
 *   OnPluginStart() is the one entry point: each section registers its own
 *   things. Everything registered belongs to this plugin and goes when it
 *   unloads, so OnPluginEnd() has nothing to undo.
 *
 * ========================================================================== */

public void OnPluginStart()
{
	LogMessage("Starting plugin.");

	SetupConVars();
	SetupCommands();
	SetupGameEvents();
	SetupCoreEvents();
	SetupNetMessages();
	SetupSounds();
	SetupGameFunctions();
	SetupEntityCommands();
	SetupTimers();
	SetupTransmit();
	SetupScriptNatives();

	LogMessage("All hooks started!");
}

public void OnPluginEnd()
{
	// Commands, listeners, event hooks, game hooks, net message hooks, the
	// sound hook, entity listeners, raw hooks, timers -- Source2Mod drops all
	// of it when the plugin goes. Unregister*/Unhook* by the id they returned
	// is for turning a feature off while the plugin stays loaded. A CConVar
	// needs no ConVar_Unregister() either: the core owns it for the script.
}

public void OnAllPluginsLoaded()
{
	/* This is where we'd do stuff that relies on the mod or other plugins
	 * being initialized (for example, cvars added and events registered).
	 */
}

public void Source2Mod_OnLevelInit(const char[] mapName, const char[] mapEntities, const char[] oldLevel, const char[] landmarkName, bool loadGame, bool background)
{
	LogMessage("OnLevelInit(%s)", mapName);
}

public void Source2Mod_OnLevelShutdown()
{
	LogMessage("OnLevelShutdown()");

	// The countdown timer is TIMER_FLAG_NO_MAPCHANGE: the scheduler kills it
	// here, and the handle with it. See SetupTimers().
	g_CountdownTimer = view_as<Source2ModTimer>(0);
}

/* ============================================================================
 *
 *   2. ConVars
 *
 *   Declaring your own, reading and writing the game's, and hearing about
 *   changes.
 *
 * ========================================================================== */

CConVar sp_sample_cvari;

// Used by the later sections, so that each of them can be tried from the console.
CConVar sp_sample_hide_bot_deaths;
CConVar sp_sample_shake_max;
CConVar sp_sample_damage_scale;

void SetupConVars()
{
	sp_sample_cvari = CConVar.CreateInt("sp_sample_cvari", FCVAR_NONE, "help string", 42);
	// Only heard about through the change hook below, so no global for it.
	CConVar.CreateFloat("sp_sample_cvarf", FCVAR_NONE, "help string", 69.69, true, 10.0, true, 100.0);

	sp_sample_hide_bot_deaths = CConVar.CreateBool("sp_sample_hide_bot_deaths", FCVAR_NONE, "Keep bot deaths out of the killfeed (4. game events)", false);
	sp_sample_shake_max = CConVar.CreateFloat("sp_sample_shake_max", FCVAR_NONE, "Largest screen shake amplitude a client is sent (6. net messages)", 25.0, true, 0.0);
	sp_sample_damage_scale = CConVar.CreateFloat("sp_sample_damage_scale", FCVAR_NONE, "Multiplier for all damage (8. game functions)", 1.0, true, 0.0);

	// One handler for every convar's change, as in C++; it filters by name.
	Source2ModConVars.HookConVarChange(OnConVarChanged);

	// A reference to a convar that exists already -- the game's here. Check it
	// before using it: a name nobody registered gives an invalid ref.
	CConVarRef limitteams = CConVarRef.Find("mp_limitteams");

	if (limitteams != null && limitteams.IsValidRef() && limitteams.IsConVarDataAvailable())
	{
		char value[32];
		limitteams.GetString(value, sizeof(value));
		LogMessage("CConVarRef \"mp_limitteams\" got value pre = %d [float = %f, bool = %d, string = \"%s\"]",
		           limitteams.IntValue, limitteams.FloatValue, limitteams.BoolValue, value);

		// Typed access: IntValue on an int convar, FloatValue on a float one ...
		limitteams.IntValue = 5;

		// ... and the converting setters work across types, as SetFloat() on
		// an int convar does in C++.
		limitteams.FloatValue = 8.5;

		limitteams.GetString(value, sizeof(value));
		LogMessage("CConVarRef \"mp_limitteams\" got value after = %d [float = %f, bool = %d, string = \"%s\"]",
		           limitteams.IntValue, limitteams.FloatValue, limitteams.BoolValue, value);
	}

	// cpp_sample's second half -- ConVarRefAbstract, SetAs<Vector>, promoting
	// an untyped ref -- has no script counterpart: a script's ConVar is never
	// typed to begin with, every value goes through the converting accessors.
}

public void OnConVarChanged(ConVar convar, int slot, const char[] newValue, const char[] oldValue)
{
	char name[64];
	convar.GetName(name, sizeof(name));

	if (!StrEqual(name, "sp_sample_cvarf"))
		return;

	LogMessage("Sample convar \"%s\" was changed from %s to %s", name, oldValue, newValue);
}

/* ============================================================================
 *
 *   3. Commands
 *
 *   A command of your own, its arguments and its caller, a chat-only
 *   trigger, and listening in on a command that is not yours.
 *
 * ========================================================================== */

// 6. net messages -- one bit per player slot, the layout the hooks get their
// recipients in.
int g_NoShakeMask[2];      // players who turned screen shakes off (!sp_noshake)
int g_VoiceMutedMask[2];   // players whose voice is dropped (sp_mute)

void SetupCommands()
{
	// A console command, and the same thing again as a chat command --
	// Source2Mod routes "!sp_command" and "/sp_command" to the same handler.
	Source2ModCommands.RegisterConCommand("sp_command", Cmd_Command);

	// Arguments and the caller. Arg(0) is the command itself, ArgS() is
	// everything after it as one string. The caller is a slot: valid for a
	// player, invalid (-1) for the server console -- CallerOf() and Reply()
	// at the top of this file are built on that.
	Source2ModCommands.RegisterConCommand("sp_echo", Cmd_Echo);

	// A chat listener has no console command behind it: it fires when the word
	// is typed into chat after a chat trigger -- "!sp_noshake" or
	// "/sp_noshake" with the default triggers; plain "sp_noshake" does not
	// fire it. The handler cannot block the message itself: a public trigger
	// lets it show, a silent one hides it whatever the handler does.
	Source2ModCommands.RegisterChatListener("sp_noshake", Chat_NoShake);

	// Listening in on a command somebody else owns -- the game's, another
	// plugin's. Pre (post = false) may answer with Action_Supersede to stop
	// the command from running at all; post only watches.
	Source2ModCommands.RegisterConListener("jointeam", Listen_JoinTeamPre, false);
	Source2ModCommands.RegisterConListener("jointeam", Listen_JoinTeamPost, true);
}

public void Cmd_Command(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	char command[256];
	args.GetCommandString(command, sizeof(command));
	LogMessage("Sample command called by %d. Command: %s", context.GetPlayerSlot().Get(), command);
}

public void Cmd_Echo(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	if (args.ArgC() < 2)
	{
		Reply(context, "Usage: sp_echo <text>");
		return;
	}

	char text[256], name[64] = "Console";
	args.ArgS(text, sizeof(text));

	CCSPlayerController caller = CallerOf(context);
	if (caller != NULL_ENTITY)
		caller.GetPlayerName(name, sizeof(name));

	Reply(context, "%s said: %s (%d arguments)", name, text, args.ArgC() - 1);
}

public void Chat_NoShake(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	CPlayerSlot slot = context.GetPlayerSlot();
	if (!slot.IsValid())
		return;

	// Used by the outgoing net message hook in section 6.
	if (PlayerMask_Has(g_NoShakeMask, slot.Get()))
		PlayerMask_Remove(g_NoShakeMask, slot.Get());
	else
		PlayerMask_Add(g_NoShakeMask, slot.Get());

	Reply(context, "Screen shakes are now %s for you.", PlayerMask_Has(g_NoShakeMask, slot.Get()) ? "off" : "on");
}

public Source2ModAction Listen_JoinTeamPre(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	// "jointeam 1" is spectator. Refusing it here is the whole of a
	// "no spectators" plugin.
	char team[8];
	Arg1(args, team, sizeof(team));

	if (args.ArgC() >= 2 && StringToInt(team) == 1 && sp_sample_cvari.IntValue == 1337)
	{
		Reply(context, "Spectating is off while sp_sample_cvari is 1337.");
		return Action_Supersede;
	}

	return Action_Ignore;
}

public Source2ModAction Listen_JoinTeamPost(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	char command[256];
	args.GetCommandString(command, sizeof(command));
	LogMessage("Slot %d ran \"%s\"", context.GetPlayerSlot().Get(), command);
	return Action_Ignore;
}

/* ============================================================================
 *
 *   4. Game events
 *
 *   Hooking before and after the engine handles an event, reading and
 *   changing its fields, keeping it from the clients, and firing one.
 *
 * ========================================================================== */

void SetupGameEvents()
{
	// Pre (post = false): the event has been created but nobody has seen it
	// yet. Fields may be changed, dontBroadcast keeps it on the server, and
	// Action_Supersede cancels it outright.
	Source2ModEvents.HookGameEvent("player_death", Event_PlayerDeathPre, false);

	// Post (post = true): the engine is done with it. The place to react.
	// This one gives everybody $1337 at the start of each round.
	Source2ModEvents.HookGameEvent("round_start", Event_RoundStartPost, true);

	Source2ModEvents.HookGameEvent("player_jump", Event_PlayerJumpPost, true);

	// Firing an event. Created through the engine's event manager, filled in,
	// then sent one of two ways:
	//
	//   * IGameEventManager2.FireEvent() -- to the server and every client,
	//                                       through every hook;
	//   * controller.FireEventToClient() -- to one player only, which the
	//                                       server itself never sees. That is
	//                                       what makes per-player HUD text out
	//                                       of show_survival_respawn_status.
	Source2ModCommands.RegisterConCommand("sp_event", Cmd_Event);
}

public Source2ModAction Event_PlayerDeathPre(IGameEvent event, bool post, bool &dontBroadcast)
{
	// Player fields resolve straight to the entity, the slot or the pawn.
	CCSPlayerController victim = event.GetPlayerController("userid");
	CCSPlayerController attacker = event.GetPlayerController("attacker");

	char victimName[64] = "?", attackerName[64] = "world", weapon[64];
	if (victim != NULL_ENTITY)
		victim.GetPlayerName(victimName, sizeof(victimName));
	if (attacker != NULL_ENTITY)
		attacker.GetPlayerName(attackerName, sizeof(attackerName));
	event.GetString("weapon", weapon, sizeof(weapon));

	LogMessage("player_death: %s killed %s with %s%s", attackerName, victimName, weapon, event.GetBool("headshot") ? " (headshot)" : "");

	// The server still processes the event -- stats, other plugins' post
	// hooks -- but no client is told, so the killfeed never shows it.
	if (sp_sample_hide_bot_deaths.BoolValue && victim != NULL_ENTITY && victim.IsBot())
	{
		dontBroadcast = true;
		return Action_Override;
	}

	return Action_Ignore;
}

public Source2ModAction Event_RoundStartPost(IGameEvent event, bool post, bool &dontBroadcast)
{
	for (int slot = 0; slot < 64; slot++)
	{
		CCSPlayerController controller = PlayerInSlot(slot);
		if (controller != NULL_ENTITY)
			controller.m_pInGameMoneyServices.m_iAccount = 1337;
	}

	return Action_Ignore;
}

public Source2ModAction Event_PlayerJumpPost(IGameEvent event, bool post, bool &dontBroadcast)
{
	LogMessage("player_jump: slot %d", event.GetPlayerSlot("userid").Get());
	return Action_Ignore;
}

public void Cmd_Event(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	CCSPlayerController caller = CallerOf(context);
	if (caller == NULL_ENTITY)
	{
		Reply(context, "This command is for players.");
		return;
	}

	IGameEvent event = IGameEventManager2.CreateEvent("show_survival_respawn_status", true);
	if (event == null)
		return;

	event.SetString("loc_token", "<font color='#00ff00'>Hello from a game event</font>");
	event.SetInt("duration", 5);
	event.SetPlayer("userid", caller.GetPlayerSlot());

	caller.FireEventToClient(event);

	// FireEvent() frees the event it is given; an event that went out any
	// other way is still yours.
	IGameEventManager2.FreeEvent(event);
}

/* ============================================================================
 *
 *   5. Core events
 *
 *   What the server itself is doing, as opposed to what the game announces:
 *   entities coming and going, entity outputs firing, clients connecting,
 *   the frame ticking. Map start and end are in section 1. And the one place
 *   this plugin hooks the engine itself, for what Source2Mod does not cover.
 *
 * ========================================================================== */

void SetupCoreEvents()
{
	// IEntityListener: created / spawned / deleted (/ parent changed), each
	// optional.
	Source2ModEntities.AddEntityListener(OnEntityCreated, OnEntitySpawned, OnEntityDeleted);

	// IEntityIOListener: a classname and an output, "" for "any". Pre may
	// return Action_Supersede, and the output then never fires -- the door
	// stays shut, the button does nothing.
	Source2ModEntities.AddEntityIOListener(OnEntityOutput, "func_button", "OnPressed");
	Source2ModEntities.AddEntityIOListener(OnEntityOutput, "", "OnStartTouch", true);

	// The client callbacks and the frame are Source2Mod_On* forwards, below;
	// two engine calls Source2Mod has no callback for are raw hooks.
	SetupRawEngineHooks();
}

public void OnEntityCreated(CEntityInstance entity)
{
	// Created is early: the entity exists, its keyvalues have not been applied
	// and it has not spawned. Good for noticing, too soon for changing.
}

public void OnEntitySpawned(CEntityInstance entity)
{
	// Every smoke grenade thrown from here on is purple.
	char classname[64];
	entity.GetClassname(classname, sizeof(classname));
	if (!StrEqual(classname, "smokegrenade_projectile"))
		return;

	// The game fills the projectile in right after spawning it, so a change
	// made here would be overwritten. One frame later it sticks. An index
	// must not cross a frame -- the entity may be gone and the index reused by
	// then -- a handle resolves to NULL_ENTITY instead.
	Source2ModScheduler.NextFrame(PaintSmoke, entity.GetRefEHandle());
}

public void PaintSmoke(any ref)
{
	CSmokeGrenadeProjectile smoke = view_as<CSmokeGrenadeProjectile>(CEntityInstance.FromRefEHandle(ref));
	if (smoke == NULL_ENTITY)
		return;

	float purple[3] = { 170.0, 0.0, 255.0 };
	smoke.Set_m_vSmokeColor(purple);
}

public void OnEntityDeleted(CEntityInstance entity)
{
	// Last call for this entity. Anything holding its index has to let go now.
}

public Source2ModAction OnEntityOutput(const char[] output, CEntityInstance activator, CEntityInstance caller, float delay, bool post)
{
	// OnStartTouch fires constantly; only the button is worth a log line.
	if (!post)
	{
		char callerClass[64] = "?", activatorClass[64] = "none";
		if (caller != NULL_ENTITY)
			caller.GetClassname(callerClass, sizeof(callerClass));
		if (activator != NULL_ENTITY)
			activator.GetClassname(activatorClass, sizeof(activatorClass));

		LogMessage("%s fired %s (activator: %s)", callerClass, output, activatorClass);
	}

	return Action_Ignore;
}

// The client and frame callbacks: forwards, called for every plugin that
// declares them. Nothing to install or take down.

public void Source2Mod_OnClientPutInServer(CPlayerSlot slot, const char[] name, int type, const char[] xuid)
{
	LogMessage("OnClientPutInServer(%d, \"%s\", %d, %s)", slot.Get(), name, type, xuid);
}

public void Source2Mod_OnClientSettingsChanged(CPlayerSlot slot)
{
	LogMessage("OnClientSettingsChanged(%d)", slot.Get());
}

public void Source2Mod_OnClientDisconnect(CPlayerSlot slot, int reason, const char[] name, const char[] xuid, const char[] networkId)
{
	LogMessage("OnClientDisconnect(%d, %d, \"%s\", %s, \"%s\")", slot.Get(), reason, name, xuid, networkId);

	// Per-slot state has to go with the player, or the next one to take the
	// slot inherits it.
	PlayerMask_Remove(g_NoShakeMask, slot.Get());
	PlayerMask_Remove(g_VoiceMutedMask, slot.Get());
}

public void Source2Mod_OnGameFrame(bool simulating, bool firstTick, bool lastTick)
{
	// Runs every tick, right after the game's own GameFrame -- 64 times a
	// second, so keep it cheap. `simulating` is false while the game is not
	// ticking (paused, hibernating). A plugin that does not declare this
	// forward costs nothing per frame.
}

// Two things Source2Mod has no callback for: turning a connection away, and
// swallowing a client command before the engine sees it. For those the
// plugin hooks the engine itself -- virtual functions of CSource2GameClients,
// cpp_sample's KHOOK_VIRTUALs. A plugin with hooks of its own needs a
// gamedata update whenever the game moves them; prefer what Source2Mod has.

void SetupRawEngineHooks()
{
	// ISource2GameClients::ClientCommand is in the core's gamedata; the slot
	// is the same on both platforms.
	int commandIndex = Source2ModGameConfig.GetOffset("ISource2GameClients::ClientCommand");
	if (commandIndex < 0)
		return;

	// bool ClientConnect(CPlayerSlot, const char *name, uint64 xuid,
	//                    const char *networkId, bool unk1, CBufferString *rejectReason)
	// -- five slots before ClientCommand, in the order of isource2server.h.
	Source2ModHook connect = Source2ModHook.FromVirtual("server", "CSource2GameClients", commandIndex - 5, Source2ModType_Bool);
	if (connect != null)
	{
		connect.AddParam(Source2ModType_Int32);          // CPlayerSlot
		connect.AddParam(Source2ModType_String);         // name
		connect.AddParam(Source2ModType_Int64);          // xuid
		connect.AddParam(Source2ModType_String);         // networkId
		connect.AddParam(Source2ModType_Bool);           // unk1
		connect.AddParam(Source2ModType_BufferString);   // rejectReason
		connect.Enable(Hook_ClientConnect);
	}

	// void ClientCommand(CPlayerSlot, const CCommand &args)
	Source2ModHook command = Source2ModHook.FromVirtual("server", "CSource2GameClients", commandIndex, Source2ModType_Void);
	if (command != null)
	{
		command.AddParam(Source2ModType_Int32);          // CPlayerSlot
		command.AddParam(Source2ModType_Command);        // const CCommand &
		command.Enable(Hook_ClientCommand);
	}
}

public Source2ModAction Hook_ClientConnect(Source2ModHookParams params, Source2ModHookReturn ret, bool post)
{
	char name[128], networkId[64], xuid[32];
	params.GetString(2, name, sizeof(name));
	params.GetString(3, xuid, sizeof(xuid));
	params.GetString(4, networkId, sizeof(networkId));
	LogMessage("Hook_ClientConnect(%d, \"%s\", %s, \"%s\", %d)", params.GetInt(1), name, xuid, networkId, params.GetBool(5));

	// A hook on a function that returns something returns the value alongside
	// the action. With Override the original still runs but the value set
	// here is what the caller gets -- here: refuse the connection and hand
	// back the reason. Try it by connecting with "sp_rejected" as your name.
	if (StrEqual(name, "sp_rejected"))
	{
		params.SetBufferString(6, "Rejected by the sample plugin.");
		ret.SetBool(false);
		return Action_Override;
	}

	// Ignore leaves the return value alone: the original's is what counts.
	return Action_Ignore;
}

public Source2ModAction Hook_ClientCommand(Source2ModHookParams params, Source2ModHookReturn ret, bool post)
{
	// Supersede blocks the original entirely -- the engine never sees this
	// command. Use it to take a command over, not to "handle it as well".
	Source2ModCommandArgs args = params.GetCommandArgs(2);

	char command[64];
	args.Arg(0, command, sizeof(command));
	if (StrEqual(command, "sp_blocked"))
	{
		LogMessage("Swallowing \"sp_blocked\" -- the engine will never see it.");
		return Action_Supersede;
	}

	// Ignore says "I did nothing": the original runs, and so does whatever else
	// is hooked here. The right answer for a hook that only looks.
	return Action_Ignore;
}

/* ============================================================================
 *
 *   6. Net messages
 *
 *   Building a protobuf message and sending it to chosen players, and sitting
 *   on the wire in both directions.
 *
 * ========================================================================== */

void SetupNetMessages()
{
	// Sending. Allocate by id (or by name, AllocateNetMessageByPartialName),
	// set fields by their .proto names, send, free. The message is yours
	// throughout: sending does not consume it, so one message can go out many
	// times.
	Source2ModCommands.RegisterConCommand("sp_shake", Cmd_Shake);

	// Server -> clients. Every message the server posts, this plugin's own
	// included, with the recipients as a mask that may be edited. The message
	// may be edited too: the clients get what is left when the hooks are
	// done. Action_Supersede drops it for everybody.
	Source2ModNetworkMessages.HookServerMessage(OnServerMessage);

	// Client -> server. Here: a voice mute. The packets of a muted player are
	// dropped before the server looks at them, so nobody hears them.
	Source2ModNetworkMessages.HookClientMessage(OnClientMessage);

	Source2ModCommands.RegisterConCommand("sp_mute", Cmd_Mute);
}

public void Cmd_Shake(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	NetMessage msg = Source2ModNetworkMessages.AllocateNetMessageByID(UM_Shake);
	if (msg == null)
		return;

	char amplitude[16];
	Arg1(args, amplitude, sizeof(amplitude));

	// CUserMessageShake, usermessages.proto
	Source2ModNetworkMessages.SetUInt32(msg, "command", 0); // SHAKE_START
	Source2ModNetworkMessages.SetFloat(msg, "amplitude", amplitude[0] ? StringToFloat(amplitude) : 10.0);
	Source2ModNetworkMessages.SetFloat(msg, "frequency", 150.0);
	Source2ModNetworkMessages.SetFloat(msg, "duration", 2.0);

	CPlayerSlot slot = context.GetPlayerSlot();
	if (slot.IsValid())
	{
		Source2ModNetworkMessages.SendMessage(msg, UM_Shake, slot);              // the caller only
	}
	else
	{
		int everybody[2];
		PlayerMask_All(everybody);
		Source2ModNetworkMessages.SendMessageToPlayers(msg, UM_Shake, everybody); // from the console: everybody
	}

	Source2ModNetworkMessages.DeallocateNetMessage(msg);
}

public Source2ModAction OnServerMessage(int clients[2], int messageId, NetMessage msg)
{
	if (messageId != UM_Shake)
		return Action_Ignore;

	// Changing a field: no shake stronger than sp_sample_shake_max.
	float max = sp_sample_shake_max.FloatValue;
	if (Source2ModNetworkMessages.GetFloat(msg, "amplitude") > max)
		Source2ModNetworkMessages.SetFloat(msg, "amplitude", max);

	// Changing the recipients: not to those who typed !sp_noshake.
	clients[0] &= ~g_NoShakeMask[0];
	clients[1] &= ~g_NoShakeMask[1];

	return Action_Ignore;
}

public Source2ModAction OnClientMessage(CPlayerSlot slot, int messageId, NetMessage msg)
{
	if (messageId == clc_VoiceData && PlayerMask_Has(g_VoiceMutedMask, slot.Get()))
		return Action_Supersede;

	return Action_Ignore;
}

public void Cmd_Mute(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	if (args.ArgC() < 2)
	{
		Reply(context, "Usage: sp_mute <slot>");
		return;
	}

	char arg[8];
	Arg1(args, arg, sizeof(arg));
	int slot = StringToInt(arg);

	CCSPlayerController target = (slot >= 0 && slot < 64) ? PlayerInSlot(slot) : NULL_ENTITY;
	if (target == NULL_ENTITY)
	{
		Reply(context, "Nobody in slot %d.", slot);
		return;
	}

	if (PlayerMask_Has(g_VoiceMutedMask, slot))
		PlayerMask_Remove(g_VoiceMutedMask, slot);
	else
		PlayerMask_Add(g_VoiceMutedMask, slot);

	char name[64];
	target.GetPlayerName(name, sizeof(name));
	Reply(context, "%s is now %s.", name, PlayerMask_Has(g_VoiceMutedMask, slot) ? "muted" : "unmuted");
}

/* ============================================================================
 *
 *   7. Sounds
 *
 *   Sound events go straight to the players picked for them, come back as a
 *   guid that stops or changes them, and the game's own can be rewritten on
 *   their way out.
 *
 * ========================================================================== */

void SetupSounds()
{
	// The short way: one call, one player, heard "in the head" -- no position.
	Source2ModCommands.RegisterConCommand("sp_sound", Cmd_Sound);

	// The long way: a sound object. Source, volume, pitch, any other public
	// parameter of the sound event, and exactly who hears it.
	Source2ModCommands.RegisterConCommand("sp_sound_world", Cmd_SoundWorld);

	// The game's own sounds, on their way to the clients. Ignore leaves one
	// alone, Override sends it with the changes made here, Supersede drops it.
	Source2ModSounds.HookSound(OnSound);
}

public void Cmd_Sound(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	char sound[128];
	Arg1(args, sound, sizeof(sound));
	if (!sound[0])
		strcopy(sound, sizeof(sound), "Weapon_AK47.Single");

	CPlayerSlot slot = context.GetPlayerSlot();
	if (slot.IsValid())
		Source2ModSounds.EmitSoundToPlayer(slot, sound);
	else
		Source2ModSounds.EmitSoundToAll(sound);
}

public void Cmd_SoundWorld(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	CCSPlayerPawn pawn = AlivePawnOf(context);
	if (pawn == NULL_ENTITY)
		return;

	ISource2ModSound sound = ISource2ModSound.New("Weapon_AK47.Single");

	sound.SetVolume(0.5);
	sound.SetPitch(2.0);

	// One of three sources:
	//   SetSourceEntityIndex(SOUND_SOURCE_LISTENER) -- at each recipient (default)
	//   SetPosition(vec)                            -- a fixed point in the world
	//   SetSourceEntity(entity)                     -- from an entity, following it
	float position[3];
	pawn.GetAbsOrigin(position);
	position[2] += 128.0;
	sound.SetPosition(position);

	// Parameters beyond volume and pitch go by the name the sound event
	// declares them under:
	//     sound.SetFloat("public.my_param", 1.0);

	sound.AddAllRecipients();
	sound.RemoveRecipient(CPlayerSlot(63)); // everybody but slot 63

	// Emit() may be called again and again; each call is a new sound with a
	// new guid. The guid is what stops it or changes it while it plays.
	int guid = sound.Emit();
	delete sound;

	Source2ModScheduler.AddTimer(0.1, Timer_QuietSound, guid);
	Source2ModScheduler.AddTimer(0.3, Timer_StopSound, guid);
}

public void Timer_QuietSound(any guid)
{
	Source2ModSounds.SetSoundVolume(guid, 0.1);
}

public void Timer_StopSound(any guid)
{
	Source2ModSounds.StopSound(guid);
}

public Source2ModAction OnSound(ISource2ModSound sound)
{
	// Sounds travel as hashes. Hash the names you care about once and compare
	// numbers.
	static int knifeHit;
	if (!knifeHit)
		knifeHit = Source2ModSounds.HashSoundName("Weapon_Knife.Hit");

	if (sound.GetNameHash() != knifeHit)
		return Action_Ignore;

	sound.SetPitch(1.5);
	return Action_Override;
}

/* ============================================================================
 *
 *   8. Game functions
 *
 *   Game functions no interface exposes. The common ones -- TakeDamage,
 *   PostThink, ... -- the core hooks for you: Source2ModGameHooks, resolved
 *   from the core's own gamedata, so a plugin never touches a signature and
 *   survives an engine update with a core update. Anything else is hooked
 *   and called by gamedata name or by a signature of your own.
 *
 * ========================================================================== */

Source2ModFunction g_SwitchTeam;

void SetupGameFunctions()
{
	// false = Pre, before the game's code. The id it returns is what
	// UnhookTakeDamage() takes it off again with.
	Source2ModGameHooks.HookTakeDamage(OnTakeDamage, false);

	// Post: runs after the game's PostThink, for every pawn every tick -- do
	// as little as possible here.
	Source2ModGameHooks.HookPostThink(OnPostThink, true);

	// Calling. A gamedata entry, the return type and the parameters -- `this`
	// first for a method -- are all it takes. Add entries of your own to the
	// gamedata and they resolve the same way.
	g_SwitchTeam = Source2ModFunction.FromGameConfig("CCSPlayerController::SwitchTeam", Source2ModType_Void);
	if (g_SwitchTeam != null)
	{
		g_SwitchTeam.AddParam(Source2ModType_Entity);   // this: the controller
		g_SwitchTeam.AddParam(Source2ModType_Int32);    // the team
	}

	Source2ModCommands.RegisterConCommand("sp_swap", Cmd_Swap);

	SetupRawGameHooks();
}

public void Cmd_Swap(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	CCSPlayerController caller = CallerOf(context);
	if (caller == NULL_ENTITY || caller.m_iTeamNum < 2)
	{
		Reply(context, "You have to be on a team.");
		return;
	}

	int newTeam = caller.m_iTeamNum == 2 ? 3 : 2;

	// Way one: the function Source2Mod already resolved, as a method of the
	// class. cpp_sample's ADDR_SWITCH_TEAM() / pCaller->SwitchTeam().
	if (args.ArgC() < 2)
	{
		caller.SwitchTeam(newTeam);
		return;
	}

	// Way two ("sp_swap gamedata"): any gamedata entry, with the types
	// spelled out by you -- cpp_sample's GAMECONFIG_RESOLVE(...).RCast<>().
	if (g_SwitchTeam != null)
		g_SwitchTeam.Call(caller, newTeam);
}

public Source2ModAction OnTakeDamage(TakeDamageContext ctx, bool post, int &result)
{
	// The context holds the call: the entity taking damage, the damage info,
	// and for a function with a return value `result`.
	char classname[64] = "?";
	if (ctx.entity != NULL_ENTITY)
		ctx.entity.GetClassname(classname, sizeof(classname));

	CTakeDamageInfo info = ctx.info;
	LogMessage("TakeDamage: %s, %.1f damage", classname, info.m_flDamage);

	// Changing an argument: this is a Pre handler, so the game runs after it,
	// with whatever is in the damage info by then.
	float scale = sp_sample_damage_scale.FloatValue;
	if (scale != 1.0)
		info.m_flDamage *= scale;

	// Blocking it: the game's code does not run, the caller gets `result`.
	//
	//     result = 0;
	//     return Action_Supersede;
	//
	// Knowing what the game would do, from Pre: CallOriginal() runs the game's
	// function now (past every hook) and returns what it returned. It really
	// runs, so answer Supersede with that value -- or it runs a second time:
	//
	//     result = ctx.CallOriginal();
	//     return Action_Supersede;

	// Ignore: the game runs as it would have.
	return Action_Ignore;
}

public Source2ModAction OnPostThink(PostThinkContext ctx, bool post)
{
	return Action_Ignore;
}

// Raw hooks of game functions -- cpp_sample's KHOOK_MEMBER / KHOOK_FUNCTION,
// one per way of getting the address. Look in Source2ModGameHooks first: what
// is there needs no hook of your own.

void SetupRawGameHooks()
{
	// 1. A function the core resolves (cpp_sample: ISource2ModAddresses):
	//    every item a player is given -- sp_give below, buying, a round's
	//    loadout. Pre.
	Source2ModHook giveNamedItem = Source2ModHook.FromGameConfig("CCSPlayer_ItemServices::GiveNamedItem", Source2ModType_Entity);
	if (giveNamedItem != null)
	{
		giveNamedItem.AddParam(Source2ModType_Object);    // this: the item services
		giveNamedItem.AddParam(Source2ModType_String);    // the item
		giveNamedItem.AddParam(Source2ModType_Int32);     // subtype
		giveNamedItem.AddParam(Source2ModType_Pointer);   // CEconItemView *
		giveNamedItem.AddParam(Source2ModType_Bool);      // force
		giveNamedItem.AddParam(Source2ModType_Pointer);
		giveNamedItem.Enable(Hook_GiveNamedItem);
	}

	//    ... and on a free function: every entity the game removes.
	Source2ModHook utilRemove = Source2ModHook.FromGameConfig("UTIL::Remove", Source2ModType_Void);
	if (utilRemove != null)
	{
		utilRemove.AddParam(Source2ModType_Entity);
		utilRemove.Enable(Hook_UtilRemove);
	}

	// 2. The name of a gamedata entry -- Source2Mod's or one you added. Its
	//    library is read and the function found by exported symbol or by
	//    pattern, whichever the entry has; a game update is a gamedata update.
	Source2ModHook switchTeam = Source2ModHook.FromGameConfig("CCSPlayerController::SwitchTeam", Source2ModType_Void);
	if (switchTeam != null)
	{
		switchTeam.AddParam(Source2ModType_Entity);
		switchTeam.AddParam(Source2ModType_Int32);
		switchTeam.Enable(Hook_SwitchTeam);
	}

	// 3. A pattern of your own, hardcoded. Only for what nothing else
	//    resolves -- it needs a rebuild whenever the game moves the function.
	//    A script has no WIN_LINUX(): the pattern for the other platform simply
	//    does not resolve, so try both.
	Source2ModHook snapViewAngles = Source2ModHook.FromSignature("server",
	    "55 48 89 E5 41 57 49 89 FF 48 89 F7 41 56 41 55 41 54 53 48 81 EC ? ? ? ? E8", Source2ModType_Void);
	if (snapViewAngles == null)
		snapViewAngles = Source2ModHook.FromSignature("server",
		    "48 89 7C 24 ? 55 48 8B EC 48 81 EC ? ? ? ? 48 8B F9 48 8D 4D ? E8 ? ? ? ? F2 0F 10 87 C8 0B 00 00", Source2ModType_Void);
	if (snapViewAngles != null)
	{
		snapViewAngles.AddParam(Source2ModType_Entity);   // this: the pawn
		snapViewAngles.AddParam(Source2ModType_Vector);   // QAngle *
		snapViewAngles.Enable(Hook_SnapViewAngles);
	}
}

public Source2ModAction Hook_GiveNamedItem(Source2ModHookParams params, Source2ModHookReturn ret, bool post)
{
	char item[64];
	params.GetString(2, item, sizeof(item));
	LogMessage("GiveNamedItem: %s", item[0] ? item : "?");

	// Action_Supersede with ret.SetEntity(NULL_ENTITY) would give nothing --
	// the caller gets no weapon back.
	return Action_Ignore;
}

public Source2ModAction Hook_UtilRemove(Source2ModHookParams params, Source2ModHookReturn ret, bool post)
{
	// That is a lot of entities, so it only looks at weapons.
	CEntityInstance entity = params.GetEntity(1);
	if (entity == NULL_ENTITY)
		return Action_Ignore;

	char classname[64];
	entity.GetClassname(classname, sizeof(classname));
	if (!strncmp(classname, "weapon_", 7))
		LogMessage("UTIL_Remove: %s", classname);

	return Action_Ignore;
}

public Source2ModAction Hook_SnapViewAngles(Source2ModHookParams params, Source2ModHookReturn ret, bool post)
{
	// A pawn's view snapped to new angles -- a teleport with angles, a
	// respawn. params.SetVector(2, ...) here changes where it looks.

	// Bots have their views snapped all the time; only players are worth a
	// log line.
	CBasePlayerPawn pawn = view_as<CBasePlayerPawn>(params.GetEntity(1));
	CCSPlayerController controller = pawn != NULL_ENTITY ? pawn.GetController() : NULL_ENTITY;
	if (controller == NULL_ENTITY || controller.IsBot())
		return Action_Ignore;

	float angles[3];
	params.GetVector(2, angles);
	LogMessage("SnapViewAngles: %.1f %.1f %.1f", angles[0], angles[1], angles[2]);
	return Action_Ignore;
}

public Source2ModAction Hook_SwitchTeam(Source2ModHookParams params, Source2ModHookReturn ret, bool post)
{
	// Pre, so the player is still on the old team here -- sp_swap's switch
	// included.
	CCSPlayerController controller = view_as<CCSPlayerController>(params.GetEntity(1));

	char name[64] = "?";
	int oldTeam = -1;
	if (controller != NULL_ENTITY)
	{
		controller.GetPlayerName(name, sizeof(name));
		oldTeam = controller.m_iTeamNum;
	}

	LogMessage("SwitchTeam: %s %d -> %d", name, oldTeam, params.GetInt(2));

	// Action_Supersede would keep the player where they are; changing the
	// team is params.SetInt(2, team) and Action_Override.
	return Action_Ignore;
}

/* ============================================================================
 *
 *   9. Entities
 *
 *   Schema fields read and written like properties (a write to a networked
 *   field tells the engine to send it), the helpers on the schema classes,
 *   and finding, creating and poking entities.
 *
 * ========================================================================== */

void SetupEntityCommands()
{
	Source2ModCommands.RegisterConCommand("sp_hp", Cmd_Hp);
	Source2ModCommands.RegisterConCommand("sp_up", Cmd_Up);
	Source2ModCommands.RegisterConCommand("sp_give", Cmd_Give);
	Source2ModCommands.RegisterConCommand("sp_strip", Cmd_Strip);
	Source2ModCommands.RegisterConCommand("sp_slay", Cmd_Slay);
	Source2ModCommands.RegisterConCommand("sp_respawn", Cmd_Respawn);

	// Finding: by classname, one after another. NULL_ENTITY starts the search,
	// the last match continues it.
	Source2ModCommands.RegisterConCommand("sp_break", Cmd_Break);

	// What is under the crosshair.
	Source2ModCommands.RegisterConCommand("sp_aim", Cmd_Aim);

	// The game rules are an entity like any other, and Source2Mod keeps it at hand.
	Source2ModCommands.RegisterConCommand("sp_endround", Cmd_EndRound);
}

public void Cmd_Hp(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	CCSPlayerPawn pawn = AlivePawnOf(context);
	if (pawn == NULL_ENTITY)
		return;

	char arg[16];
	Arg1(args, arg, sizeof(arg));

	// Reading is a property get, writing a property set. += -= |= and the
	// rest work too, and all of them mark the field as changed.
	int old = pawn.m_iHealth;
	pawn.m_iHealth = arg[0] ? StringToInt(arg) : 100;

	Reply(context, "Health: %d -> %d", old, pawn.m_iHealth);
}

public void Cmd_Up(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	CCSPlayerPawn pawn = AlivePawnOf(context);
	if (pawn == NULL_ENTITY)
		return;

	// Position, angles, velocity; NULL_VECTOR keeps what the entity has.
	float target[3];
	pawn.GetAbsOrigin(target);
	target[2] += 100.0;
	pawn.Teleport(target, NULL_VECTOR, NULL_VECTOR);
}

public void Cmd_Give(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	CCSPlayerPawn pawn = AlivePawnOf(context);
	if (pawn == NULL_ENTITY)
		return;

	// The services hang off the pawn as properties, null on a pawn that does
	// not have them.
	CPlayer_ItemServices items = pawn.m_pItemServices;
	if (items.IsNull)
		return;

	char item[64];
	Arg1(args, item, sizeof(item));
	if (!item[0])
		strcopy(item, sizeof(item), "weapon_ak47");

	if (items.GiveNamedItem(item) == NULL_ENTITY)
		Reply(context, "Could not give \"%s\".", item);
}

public void Cmd_Strip(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	CCSPlayerPawn pawn = AlivePawnOf(context);
	if (pawn == NULL_ENTITY)
		return;

	CPlayer_ItemServices items = pawn.m_pItemServices;
	if (!items.IsNull)
		items.RemoveWeapons(false);
}

public void Cmd_Slay(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	CCSPlayerPawn pawn = AlivePawnOf(context);
	if (pawn != NULL_ENTITY)
		pawn.CommitSuicide(false, true);
}

public void Cmd_Respawn(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	CCSPlayerController caller = CallerOf(context);
	if (caller != NULL_ENTITY)
		caller.Respawn();
}

public void Cmd_Break(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	int broken = 0;

	CBaseEntity entity = NULL_ENTITY;
	while ((entity = Source2ModEntities.FindEntityByClassname(entity, "func_breakable")) != NULL_ENTITY)
	{
		// An input, exactly as a map's I/O would send it. AddEntityIOEvent()
		// takes the same arguments plus a delay and queues it instead.
		entity.AcceptInput("Break");
		broken++;
	}

	Reply(context, "Broke %d func_breakable.", broken);
}

public void Cmd_Aim(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	CCSPlayerController caller = CallerOf(context);
	if (caller == NULL_ENTITY)
		return;

	CBaseEntity target = Source2ModEntities.FindPickerEntity(caller);
	if (target == NULL_ENTITY)
	{
		Reply(context, "Nothing there.");
		return;
	}

	char classname[64];
	target.GetClassname(classname, sizeof(classname));
	Reply(context, "#%d %s, health %d", target.GetIndex(), classname, target.m_iHealth);
}

public void Cmd_EndRound(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	CCSGameRules rules = Source2ModEntities.GetGameRules();
	if (!rules.IsNull)
		rules.TerminateRound(5.0, 10 /* round draw */);
}

/* ============================================================================
 *
 *   10. Timers
 *
 *   Next frame, once after a delay, or repeating. All of them run on the
 *   main thread, and all of them die with the plugin.
 *
 * ========================================================================== */

Source2ModTimer g_CountdownTimer;
int g_Countdown;

void SetupTimers()
{
	// NextFrame is in OnEntitySpawned() (section 5), one-shot timers are in
	// sp_sound_world (section 7). This is the one that repeats.
	Source2ModCommands.RegisterConCommand("sp_countdown", Cmd_Countdown);
}

public void Cmd_Countdown(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	// A Source2ModTimer is good until the timer ends. A one-shot ends after it
	// fires, a repeating one when it is killed -- by you, or by a map change
	// if it carries TIMER_FLAG_NO_MAPCHANGE, which is why
	// Source2Mod_OnLevelShutdown() forgets this handle.
	if (g_CountdownTimer)
	{
		Source2ModScheduler.KillTimer(g_CountdownTimer);
		g_CountdownTimer = view_as<Source2ModTimer>(0);
	}

	char arg[8];
	Arg1(args, arg, sizeof(arg));
	g_Countdown = arg[0] ? StringToInt(arg) : 5;

	g_CountdownTimer = Source2ModScheduler.AddTimer(1.0, Timer_Countdown, 0, TIMER_FLAG_REPEAT | TIMER_FLAG_NO_MAPCHANGE);
}

public void Timer_Countdown(any data)
{
	char message[64];
	if (g_Countdown > 0)
		Format(message, sizeof(message), "%d...", g_Countdown);
	else
		strcopy(message, sizeof(message), "Go!");

	for (int slot = 0; slot < 64; slot++)
	{
		CCSPlayerController controller = PlayerInSlot(slot);
		if (controller != NULL_ENTITY)
			controller.PrintToCenter(message);
	}

	// A timer may kill itself from inside its own callback.
	if (g_Countdown-- <= 0)
	{
		Source2ModScheduler.KillTimer(g_CountdownTimer);
		g_CountdownTimer = view_as<Source2ModTimer>(0);
	}
}

/* ============================================================================
 *
 *   11. Transmit
 *
 *   Which entities each player receives. An entity is hooked once and then
 *   shown or hidden per viewer; a player is hidden through their controller
 *   and their pawn goes with it. For anything else, the CheckTransmit hook
 *   hands over each viewer's transmit set.
 *
 * ========================================================================== */

void SetupTransmit()
{
	// sp_hide <slot>: toggles whether the caller sees that player. The
	// controller is what gets hooked -- its visibility is applied to the pawn
	// it drives, the controller itself keeps going so the scoreboard is whole.
	Source2ModCommands.RegisterConCommand("sp_hide", Cmd_Hide);

	// The raw way, for what the model above does not cover: every viewer's
	// transmit set, once per tick. This one keeps the caller's own weapons
	// from everybody else -- the kind of rule that depends on both ends.
	Source2ModTransmit.HookCheckTransmit(OnCheckTransmit);
}

public void Cmd_Hide(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	CCSPlayerController caller = CallerOf(context);
	if (caller == NULL_ENTITY)
	{
		Reply(context, "Only from the game.");
		return;
	}

	if (args.ArgC() < 2)
	{
		Reply(context, "Usage: sp_hide <slot>");
		return;
	}

	char arg[8];
	Arg1(args, arg, sizeof(arg));
	int slot = StringToInt(arg);

	CCSPlayerController target = (slot >= 0 && slot < 64) ? PlayerInSlot(slot) : NULL_ENTITY;
	if (target == NULL_ENTITY)
	{
		Reply(context, "Nobody in slot %d.", slot);
		return;
	}

	// Hooking twice is refused, so ask first. The hook dies with the entity
	// and with the plugin; nothing to undo in OnPluginEnd().
	if (!Source2ModTransmit.IsEntityHooked(target))
		Source2ModTransmit.HookEntity(target);

	CPlayerSlot viewer = caller.GetPlayerSlot();
	bool hidden = !Source2ModTransmit.IsVisible(target, viewer);

	// Channel 0 is enough for one reason. A second feature hiding the same
	// player would use its own channel, so that neither un-hides the other's
	// work: the player shows only when no channel hides them.
	Source2ModTransmit.SetVisible(target, viewer, hidden, 0);

	char name[64];
	target.GetPlayerName(name, sizeof(name));
	Reply(context, "%s is now %s for you.", name, hidden ? "visible" : "hidden");
}

public void OnCheckTransmit(ISource2ModTransmitInfoList infos, int infoCount, const int[] entityIndices, int entityCount)
{
	// It runs every tick for every viewer, so the expensive part (who owns
	// what) is done once at the top and only the bit work is per viewer.
	int[] ownedIndex = new int[entityCount];
	int[] ownerSlot = new int[entityCount];
	int owned = 0;

	char classname[16];
	for (int e = 0; e < entityCount; e++)
	{
		CBaseEntity entity = CBaseEntity.FromIndex(entityIndices[e]);
		if (entity == NULL_ENTITY)
			continue;

		entity.GetClassname(classname, sizeof(classname));
		if (strncmp(classname, "weapon_", 7) != 0)
			continue;

		CBasePlayerPawn owner = view_as<CBasePlayerPawn>(entity.m_hOwnerEntity);
		if (owner == NULL_ENTITY)
			continue;

		CCSPlayerController controller = owner.GetController();
		if (controller == NULL_ENTITY)
			continue;

		ownedIndex[owned] = entityIndices[e];
		ownerSlot[owned] = controller.GetPlayerSlot().Get();
		owned++;
	}

	for (int i = 0; i < infoCount; i++)
	{
		ISource2ModTransmitInfo info = infos.Get(i);
		int viewerSlot = info.GetPlayerSlot().Get();

		for (int o = 0; o < owned; o++)
		{
			// BlockTransmit() also tells the client to keep a record of the
			// entity, so it comes back later without a "missing client entity"
			// crash. Never just clear the transmit bit.
			if (ownerSlot[o] != viewerSlot)
				info.BlockTransmit(ownedIndex[o]);
		}
	}
}

/* ============================================================================
 *
 *   12. Script natives
 *
 *   cpp_sample's section 12 from the other side: natives a C++ plugin
 *   registered, a library to test for, and a handle type of its own. Its
 *   cpp_sample.inc declares them optional, so this plugin loads without it;
 *   LibraryExists() says whether they are there.
 *
 * ========================================================================== */

void SetupScriptNatives()
{
	Source2ModCommands.RegisterConCommand("sp_natives", Cmd_Natives);
}

public void Cmd_Natives(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	if (!LibraryExists("cpp_sample"))
	{
		Reply(context, "cpp_sample is not loaded.");
		return;
	}

	char echo[128];
	Sample_Echo(echo, sizeof(echo), "%d + %d = %d", 2, 3, Sample_Add(2, 3));
	Reply(context, "Sample_Echo: %s", echo);

	// A handle of cpp_sample's type: closed by delete, or with this plugin.
	Handle counter = Sample_CreateCounter(10);
	Sample_Increment(counter);
	Reply(context, "Counter: %d", Sample_Increment(counter));
	delete counter;
}

public void OnLibraryAdded(const char[] name)
{
	if (StrEqual(name, "cpp_sample"))
		LogMessage("cpp_sample is here: its natives work from now on.");
}

public void OnLibraryRemoved(const char[] name)
{
	if (StrEqual(name, "cpp_sample"))
		LogMessage("cpp_sample is gone: its natives are errors until it is back.");
}
