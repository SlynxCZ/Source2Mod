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
 *  13. Paths, permissions  the folders, permission checks, groups, a gated command
 *  14. Traces           a ray from the eyes, what it hit, contents at a point
 *  15. Game systems, addresses  finding a game system, calling a resolved function
 *  16. JSON, HTTP       parsing a document, a GET with headers and a JSON reply
 *  17. MySQL            serverless query with parameters, connection, transaction
 *  18. Menus, custom HUD  a center HTML menu, clicks on a custom HUD layout
 *  19. Scripts          cs_script source run from the plugin, messages both ways
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
	Source2ModApi.FormatArgs(buffer, sizeof(buffer), format, 3);

	CCSPlayerController caller = CallerOf(context);
	if (caller != NULL_ENTITY)
		caller.PrintToChat(buffer);
	else
		Source2ModApi.ConPrintf("%s", buffer);
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
	Source2ModApi.Log("Starting plugin.");

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
	SetupPathsAndPermissions();
	SetupTraces();
	SetupGameSystems();
	SetupJsonAndHttp();
	SetupMySQL();
	SetupMenus();
	SetupScripts();

	Source2ModApi.Log("All hooks started!");
}

public void OnPluginEnd()
{
	// Commands, listeners, event hooks, game hooks, net message hooks, the
	// sound hook, entity listeners, raw hooks, timers -- Source2Mod drops all
	// of it when the plugin goes. Unregister*/Unhook* by the id they returned
	// is for turning a feature off while the plugin stays loaded. A CConVar
	// needs no ConVar_Unregister() either: the core owns it for the script.
}

public void Source2Mod_OnAllSource2ModPluginsLoaded()
{
	/* This is where we'd do stuff that relies on the mod or other plugins
	 * being initialized (for example, cvars added and events registered).
	 */
}

public void Source2Mod_OnLevelInit(const char[] mapName, const char[] mapEntities, const char[] oldLevel, const char[] landmarkName, bool loadGame, bool background)
{
	Source2ModApi.Log("OnLevelInit(%s)", mapName);
}

public void Source2Mod_OnLevelShutdown()
{
	Source2ModApi.Log("OnLevelShutdown()");

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
		Source2ModApi.Log("CConVarRef \"mp_limitteams\" got value pre = %d [float = %f, bool = %d, string = \"%s\"]",
		           limitteams.IntValue, limitteams.FloatValue, limitteams.BoolValue, value);

		// Typed access: IntValue on an int convar, FloatValue on a float one ...
		limitteams.IntValue = 5;

		// ... and the converting setters work across types, as SetFloat() on
		// an int convar does in C++.
		limitteams.FloatValue = 8.5;

		limitteams.GetString(value, sizeof(value));
		Source2ModApi.Log("CConVarRef \"mp_limitteams\" got value after = %d [float = %f, bool = %d, string = \"%s\"]",
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

	if (!Source2ModUtils.Equal(name, "sp_sample_cvarf"))
		return;

	Source2ModApi.Log("Sample convar \"%s\" was changed from %s to %s", name, oldValue, newValue);
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
	Source2ModApi.Log("Sample command called by %d. Command: %s", context.GetPlayerSlot().Get(), command);
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

	if (args.ArgC() >= 2 && Source2ModUtils.StringToInt(team) == 1 && sp_sample_cvari.IntValue == 1337)
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
	Source2ModApi.Log("Slot %d ran \"%s\"", context.GetPlayerSlot().Get(), command);
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

	Source2ModApi.Log("player_death: %s killed %s with %s%s", attackerName, victimName, weapon, event.GetBool("headshot") ? " (headshot)" : "");

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
	Source2ModApi.Log("player_jump: slot %d", event.GetPlayerSlot("userid").Get());
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
	if (!Source2ModUtils.Equal(classname, "smokegrenade_projectile"))
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

		Source2ModApi.Log("%s fired %s (activator: %s)", callerClass, output, activatorClass);
	}

	return Action_Ignore;
}

// The client and frame callbacks: forwards, called for every plugin that
// declares them. Nothing to install or take down.

// Pre ClientConnect, players only. Returning false turns the connection away
// with the reason written into rejectReason -- try it by connecting with
// "sp_rejected" as your name.
public bool Source2Mod_OnClientConnect(CPlayerSlot slot, const char[] name, const char[] xuid, const char[] networkId, bool unk1, char[] rejectReason, int maxlen)
{
	Source2ModApi.Log("OnClientConnect(%d, \"%s\", %s, \"%s\")", slot.Get(), name, xuid, networkId);

	if (Source2ModUtils.Equal(name, "sp_rejected"))
	{
		Source2ModUtils.Copy(rejectReason, maxlen, "Rejected by the sample plugin.");
		return false;
	}
	return true;
}

// The slot is taken -- by bots too, which never go through OnClientConnect.
public void Source2Mod_OnClientConnected(CPlayerSlot slot, const char[] name, const char[] xuid, const char[] networkId, const char[] address, bool fakePlayer)
{
	Source2ModApi.Log("OnClientConnected(%d, \"%s\", %s, \"%s\", %d)", slot.Get(), name, address, networkId, fakePlayer);
}

public void Source2Mod_OnClientPutInServer(CPlayerSlot slot, const char[] name, int type, const char[] xuid)
{
	Source2ModApi.Log("OnClientPutInServer(%d, \"%s\", %d, %s)", slot.Get(), name, type, xuid);
}

public void Source2Mod_OnClientSettingsChanged(CPlayerSlot slot)
{
	Source2ModApi.Log("OnClientSettingsChanged(%d)", slot.Get());
}

public void Source2Mod_OnClientDisconnect(CPlayerSlot slot, int reason, const char[] name, const char[] xuid, const char[] networkId)
{
	Source2ModApi.Log("OnClientDisconnect(%d, %d, \"%s\", %s, \"%s\")", slot.Get(), reason, name, xuid, networkId);

	// Per-slot state has to go with the player, or the next one to take the
	// slot inherits it.
	PlayerMask_Remove(g_NoShakeMask, slot.Get());
	PlayerMask_Remove(g_VoiceMutedMask, slot.Get());
}

// After the engine let the player go; OnClientDisconnect (before it) is the
// last look at what they had.
public void Source2Mod_OnClientDisconnectPost(CPlayerSlot slot, int reason, const char[] name, const char[] xuid, const char[] networkId)
{
	Source2ModApi.Log("OnClientDisconnectPost(%d, %d, \"%s\")", slot.Get(), reason, name);
}

public void Source2Mod_OnGameFrame(bool simulating, bool firstTick, bool lastTick)
{
	// Runs every tick, right after the game's own GameFrame -- 64 times a
	// second, so keep it cheap. `simulating` is false while the game is not
	// ticking (paused, hibernating). A plugin that does not declare this
	// forward costs nothing per frame.
}

// Something Source2Mod has no callback for: swallowing a client command
// before the engine sees it. For that the plugin hooks the engine itself --
// a virtual function of CSource2GameClients, like cpp_sample's KHOOK_VIRTUALs. A plugin with hooks of its own needs a
// gamedata update whenever the game moves them; prefer what Source2Mod has.

void SetupRawEngineHooks()
{
	// ISource2GameClients::ClientCommand is in the core's gamedata; the slot
	// is the same on both platforms.
	int commandIndex = Source2ModGameConfig.GetOffset("ISource2GameClients::ClientCommand");
	if (commandIndex < 0)
		return;

	// A member function's first parameter is `this`; every other index
	// counts from there.

	// void ClientCommand(CPlayerSlot, const CCommand &args)
	Source2ModHook command = Source2ModHook.FromVirtual("server", "CSource2GameClients", commandIndex, Source2ModType_Void);
	if (command != null)
	{
		command.AddParam(Source2ModType_Pointer);        // this
		command.AddParam(Source2ModType_Int32);          // CPlayerSlot
		command.AddParam(Source2ModType_Command);        // const CCommand &
		command.Enable(Hook_ClientCommand);
	}
}

public Source2ModAction Hook_ClientCommand(Source2ModHookParams params, Source2ModHookReturn ret, bool post)
{
	// Supersede blocks the original entirely -- the engine never sees this
	// command. Use it to take a command over, not to "handle it as well".
	Source2ModCommandArgs args = params.GetCommandArgs(3);

	char command[64];
	args.Arg(0, command, sizeof(command));
	if (Source2ModUtils.Equal(command, "sp_blocked"))
	{
		Source2ModApi.Log("Swallowing \"sp_blocked\" -- the engine will never see it.");
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
	Source2ModNetworkMessages.SetFloat(msg, "amplitude", amplitude[0] ? Source2ModUtils.StringToFloat(amplitude) : 10.0);
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
	int slot = Source2ModUtils.StringToInt(arg);

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
		Source2ModUtils.Copy(sound, sizeof(sound), "Weapon_AK47.Single");

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
	Source2ModApi.Log("TakeDamage: %s, %.1f damage", classname, info.m_flDamage);

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
	Source2ModApi.Log("GiveNamedItem: %s", item[0] ? item : "?");

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
	if (!Source2ModUtils.CompareN(classname, "weapon_", 7))
		Source2ModApi.Log("UTIL_Remove: %s", classname);

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
	Source2ModApi.Log("SnapViewAngles: %.1f %.1f %.1f", angles[0], angles[1], angles[2]);
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

	Source2ModApi.Log("SwitchTeam: %s %d -> %d", name, oldTeam, params.GetInt(2));

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
	pawn.m_iHealth = arg[0] ? Source2ModUtils.StringToInt(arg) : 100;

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
		Source2ModUtils.Copy(item, sizeof(item), "weapon_ak47");

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
	g_Countdown = arg[0] ? Source2ModUtils.StringToInt(arg) : 5;

	g_CountdownTimer = Source2ModScheduler.AddTimer(1.0, Timer_Countdown, 0, TIMER_FLAG_REPEAT | TIMER_FLAG_NO_MAPCHANGE);
}

public void Timer_Countdown(any data)
{
	char message[64];
	if (g_Countdown > 0)
		Source2ModApi.Format(message, sizeof(message), "%d...", g_Countdown);
	else
		Source2ModUtils.Copy(message, sizeof(message), "Go!");

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
	int slot = Source2ModUtils.StringToInt(arg);

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
		if (Source2ModUtils.CompareN(classname, "weapon_", 7) != 0)
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
 *   Source2ModNatives.LibraryExists() says whether they are there.
 *
 * ========================================================================== */

void SetupScriptNatives()
{
	Source2ModCommands.RegisterConCommand("sp_natives", Cmd_Natives);
}

public void Cmd_Natives(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	if (!Source2ModNatives.LibraryExists("cpp_sample"))
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
	if (Source2ModUtils.Equal(name, "cpp_sample"))
		Source2ModApi.Log("cpp_sample is here: its natives work from now on.");
}

public void OnLibraryRemoved(const char[] name)
{
	if (Source2ModUtils.Equal(name, "cpp_sample"))
		Source2ModApi.Log("cpp_sample is gone: its natives are errors until it is back.");
}

/* ============================================================================
 *
 *   Shared helper for the asynchronous sections (16, 17)
 *
 * ========================================================================== */

// A reply that arrives frames later: the command context is gone by then, so
// the slot travels with the request (as `data`) -- the player's chat, or the
// console for -1. The player may have left in between; then nobody is told.
void ReplyToSlot(int slot, const char[] format, any ...)
{
	char buffer[512];
	Source2ModApi.FormatArgs(buffer, sizeof(buffer), format, 3);

	if (slot < 0)
	{
		Source2ModApi.ConPrintf("%s", buffer);
		return;
	}

	CCSPlayerController player = PlayerInSlot(slot);
	if (player != NULL_ENTITY)
		player.PrintToChat(buffer);
}

int SlotOf(Source2ModCommandContext context)
{
	CPlayerSlot slot = context.GetPlayerSlot();
	return slot.IsValid() ? slot.Get() : -1;
}

/* ============================================================================
 *
 *   13. Paths and permissions
 *
 *   Where Source2Mod keeps its files, and who may do what: dotted
 *   permissions ("sp_sample.admin"), groups that hold them, immunity, and
 *   commands that check them for you. What this plugin creates or grants is
 *   tagged with it. SteamIDs are 64-bit, so they are decimal text here.
 *
 * ========================================================================== */

void SetupPathsAndPermissions()
{
	// Absolute paths, no trailing separator. PluginConfigDirectory() is this
	// plugin's own folder under configs/, made on first use.
	char path[256];
	Source2ModPaths.RootDirectory(path, sizeof(path));
	Source2ModApi.Log("Root: %s", path);
	Source2ModPaths.ConfigsDirectory(path, sizeof(path));
	Source2ModApi.Log("Configs: %s", path);
	Source2ModPaths.PluginConfigDirectory(path, sizeof(path));
	Source2ModApi.Log("My configs: %s", path);

	// A group of our own that holds a permission of our own. permissions.json
	// can grant the same group to admins; sp_vip below adds you at runtime.
	Source2ModPermissions.CreateGroup("sp_sample_vip", 10);
	Source2ModPermissions.AddGroupPermission("sp_sample_vip", "sp_sample.admin");

	// A command with a permission: the core checks it before the handler
	// runs, and permissions.json's "Overrides" can re-assign it.
	Source2ModCommands.RegisterConCommand("sp_admin", Cmd_Admin, "sp_sample.admin");

	// What a player who lacks it is told, instead of the core's default line.
	Source2ModPermissions.SetAccessDeniedHandler(OnAccessDenied);

	// What the caller holds.
	Source2ModCommands.RegisterConCommand("sp_perm", Cmd_Perm);

	// Joining and leaving the group. Runtime grants are not written to
	// permissions.json; "s2m admins reload" keeps them.
	Source2ModCommands.RegisterConCommand("sp_vip", Cmd_Vip);
}

public void Cmd_Admin(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	Reply(context, "You have sp_sample.admin.");
}

public void OnAccessDenied(Source2ModCommandContext context, Source2ModCommandArgs args, const char[] permission)
{
	char command[64];
	args.Arg(0, command, sizeof(command));
	Reply(context, "%s needs %s -- try sp_vip.", command, permission);
}

public void Cmd_Perm(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	CPlayerSlot slot = context.GetPlayerSlot();
	if (!slot.IsValid())
	{
		Reply(context, "The console holds everything.");
		return;
	}

	// "0" until the player is authorized with Steam.
	char steamId[32], permission[64];
	Source2ModPermissions.GetPlayerSteamID(slot, steamId, sizeof(steamId));
	Arg1(args, permission, sizeof(permission));
	if (!permission[0])
		Source2ModUtils.Copy(permission, sizeof(permission), "sp_sample.admin");

	Reply(context, "%s: authorized %d, %s %d, sp_sample_vip %d, immunity %d",
	      steamId, Source2ModPermissions.IsPlayerAuthorized(slot), permission,
	      Source2ModPermissions.PlayerHasPermission(slot, permission),
	      Source2ModPermissions.InGroup(steamId, "sp_sample_vip"),
	      Source2ModPermissions.GetImmunity(steamId));
}

public void Cmd_Vip(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	CPlayerSlot slot = context.GetPlayerSlot();
	char steamId[32] = "0";
	if (slot.IsValid())
		Source2ModPermissions.GetPlayerSteamID(slot, steamId, sizeof(steamId));

	if (Source2ModUtils.Equal(steamId, "0"))
	{
		Reply(context, "For authorized players only.");
		return;
	}

	if (Source2ModPermissions.InGroup(steamId, "sp_sample_vip"))
		Source2ModPermissions.RemoveFromGroup(steamId, "sp_sample_vip");
	else
		Source2ModPermissions.AddToGroup(steamId, "sp_sample_vip");

	Reply(context, "sp_sample_vip: %s", Source2ModPermissions.InGroup(steamId, "sp_sample_vip") ? "in" : "out");
}

/* ============================================================================
 *
 *   14. Traces
 *
 *   A ray (or a box) through the world: what it hit, where, and what the
 *   world is made of at a point. Options and results are handles.
 *
 * ========================================================================== */

void SetupTraces()
{
	Source2ModCommands.RegisterConCommand("sp_trace", Cmd_Trace);
}

public void Cmd_Trace(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	CCSPlayerPawn pawn = AlivePawnOf(context);
	if (pawn == NULL_ENTITY)
		return;

	// From the eyes, along the view, until something stops it. MASK_SHOT is
	// the default -- what a bullet hits; the pawn itself is skipped.
	float start[3], angles[3];
	pawn.GetEyePosition(start);
	pawn.GetEyeAngles(angles);

	TraceOptions options = new TraceOptions();
	TraceResult tr = Source2ModTrace.TraceShape(start, angles, pawn, options);
	delete options;

	if (!tr.DidHit())
	{
		Reply(context, "Nothing in the way.");
		delete tr;
		return;
	}

	char hit[64] = "the world";
	CEntityInstance hitEntity = tr.HitEntity();
	if (hitEntity != NULL_ENTITY)
		hitEntity.GetClassname(hit, sizeof(hit));

	float end[3];
	tr.EndPos(end);
	delete tr;

	// The contents at the end point, filtered by a mask: is it water?
	int water[2], contents[2];
	Mask_Water(water);
	Source2ModTrace.PointContents(end, water, contents);
	bool inWater = contents[0] != 0 || contents[1] != 0;

	Reply(context, "Hit %s at %.0f units (%.0f %.0f %.0f)%s",
	      hit, Source2ModMath.VectorDistance(start, end), end[0], end[1], end[2], inWater ? ", in water" : "");
}

/* ============================================================================
 *
 *   15. Game systems and addresses
 *
 *   Game systems are engine singletons, reached by their factory name. And
 *   the game functions the core resolved from its own gamedata
 *   (Address_*): no signature of your own, a core update after a game update.
 *
 * ========================================================================== */

Source2ModFunction g_UtilRemove;

void SetupGameSystems()
{
	if (Source2ModGameSystems.IsAvailable())
	{
		// Only systems whose factory keeps a static instance can be found;
		// SoundOpGameSystem does, GameRulesGameSystem for one does not.
		SchemaObject soundOps = Source2ModGameSystems.FindByName("SoundOpGameSystem");
		Source2ModApi.Log("SoundOpGameSystem: %s", soundOps.IsNull ? "missing" : "found");
	}

	// UTIL_Remove by the address the core resolved -- section 8 calls by
	// gamedata name, this is the third way. Unresolved on this game version =
	// the function stays null.
	if (Source2ModAddresses.IsResolved(Address_UTIL_Remove))
	{
		g_UtilRemove = Source2ModFunction.FromAddress(Address_UTIL_Remove, Source2ModType_Void);
		if (g_UtilRemove != null)
			g_UtilRemove.AddParam(Source2ModType_Entity);
	}

	// sp_remove: removes what is under the crosshair.
	Source2ModCommands.RegisterConCommand("sp_remove", Cmd_Remove);
}

public void Cmd_Remove(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	CCSPlayerController caller = CallerOf(context);
	if (caller == NULL_ENTITY)
		return;

	char classname[64];
	CBaseEntity target = Source2ModEntities.FindPickerEntity(caller);
	if (target != NULL_ENTITY)
		target.GetClassname(classname, sizeof(classname));

	if (target == NULL_ENTITY || Source2ModUtils.Equal(classname, "player"))
	{
		Reply(context, "Aim at something that is not a player.");
		return;
	}

	if (g_UtilRemove != null)
	{
		Reply(context, "Removing #%d %s.", target.GetIndex(), classname);
		g_UtilRemove.Call(target);
	}
}

/* ============================================================================
 *
 *   16. JSON and HTTP
 *
 *   Parsing a document and walking its values; a request with headers whose
 *   answer comes back on a later frame, already parsed when it is JSON.
 *
 * ========================================================================== */

void SetupJsonAndHttp()
{
	// A document owns its values: read them while it lives, delete it once.
	ISource2ModJSONDocument doc = Source2ModJSON.Parse("{ \"name\": \"sp_sample\", \"sections\": [1, 2, 3], \"nested\": { \"ok\": true } }");
	if (doc.IsValid())
	{
		ISource2ModJSONValue root = doc.Root();
		ISource2ModJSONValue sections = root.Get("sections");
		ISource2ModJSONValue nested = root.Get("nested");

		int sum = 0;
		int count = sections != null ? sections.Count() : 0;
		for (int i = 0; i < count; i++)
			sum += sections.At(i).AsInt();

		char name[64];
		root.GetString("name", name, sizeof(name));
		Source2ModApi.Log("JSON: %s, %d sections summing to %d, nested.ok %d", name, count, sum, nested != null ? nested.GetBool("ok") : false);
	}
	else
	{
		char error[256];
		doc.GetError(error, sizeof(error));
		Source2ModApi.Log("JSON error: %s", error);
	}
	delete doc;

	// sp_http [url]: a GET through Steam's HTTP client, off the game thread;
	// the callback runs on a game frame. GitHub wants a User-Agent.
	Source2ModCommands.RegisterConCommand("sp_http", Cmd_Http);
}

public void Cmd_Http(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	if (!Source2ModHTTP.IsAvailable())
	{
		Reply(context, "Steam's HTTP client is not up yet.");
		return;
	}

	char url[256];
	Arg1(args, url, sizeof(url));
	if (!url[0])
		Source2ModUtils.Copy(url, sizeof(url), "https://api.github.com/repos/alliedmodders/sourcepawn");

	// The request copies the headers: they can go right after.
	Source2ModHTTPHeaders headers = new Source2ModHTTPHeaders();
	headers.Add("User-Agent", "Source2Mod-sp_sample");
	Source2ModHTTP.Get(url, OnHttpResponse, SlotOf(context), headers);
	delete headers;
}

public void OnHttpResponse(Source2ModHTTPResponse response, any slot)
{
	if (!response.Success())
	{
		ReplyToSlot(slot, "The request failed.");
		return;
	}

	// Json() is the body parsed, valid during this callback only.
	ISource2ModJSONValue json = response.Json();
	if (json != null)
	{
		char fullName[128];
		json.GetString("full_name", fullName, sizeof(fullName), "?");
		ReplyToSlot(slot, "HTTP %d: %s, %d stars", response.StatusCode(), fullName, json.GetInt("stargazers_count"));
	}
	else
	{
		ReplyToSlot(slot, "HTTP %d: %d bytes, not JSON", response.StatusCode(), response.BodyLength());
	}
}

/* ============================================================================
 *
 *   17. MySQL
 *
 *   Queries run off the game thread; their callbacks come back on a game
 *   frame. Serverless connects per query; a connection stays open and runs
 *   transactions. Set sp_sample_mysql_* to a server to try it.
 *
 * ========================================================================== */

CConVar sp_sample_mysql_host;
CConVar sp_sample_mysql_user;
CConVar sp_sample_mysql_pass;
CConVar sp_sample_mysql_db;

// Made on first use, gone with the plugin.
ISource2ModSQLServerless g_Serverless;
ISource2ModSQLConnection g_Connection;

void SetupMySQL()
{
	sp_sample_mysql_host = CConVar.CreateString("sp_sample_mysql_host", FCVAR_PROTECTED, "MySQL host for 17. MySQL (empty = off)", "");
	sp_sample_mysql_user = CConVar.CreateString("sp_sample_mysql_user", FCVAR_PROTECTED, "MySQL user", "root");
	sp_sample_mysql_pass = CConVar.CreateString("sp_sample_mysql_pass", FCVAR_PROTECTED, "MySQL password", "");
	sp_sample_mysql_db = CConVar.CreateString("sp_sample_mysql_db", FCVAR_PROTECTED, "MySQL database", "test");

	// sp_mysql [text]: one query with a "?" parameter, escaped for you.
	Source2ModCommands.RegisterConCommand("sp_mysql", Cmd_MySQL);

	// sp_mysql_txn: a connection, then two queries as one transaction.
	Source2ModCommands.RegisterConCommand("sp_mysql_txn", Cmd_MySQLTxn);
}

// The four convars, or false (and the caller told) when the host is empty.
bool MySQLSettings(Source2ModCommandContext context, char host[64], char user[64], char pass[64], char db[64])
{
	sp_sample_mysql_host.GetString(host, sizeof(host));
	if (!host[0])
	{
		Reply(context, "Set sp_sample_mysql_host (and _user, _pass, _db) first.");
		return false;
	}

	sp_sample_mysql_user.GetString(user, sizeof(user));
	sp_sample_mysql_pass.GetString(pass, sizeof(pass));
	sp_sample_mysql_db.GetString(db, sizeof(db));
	return true;
}

public void Cmd_MySQL(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	char host[64], user[64], pass[64], db[64];
	if (!MySQLSettings(context, host, user, pass, db))
		return;

	if (g_Serverless == null)
		g_Serverless = Source2ModSQL.CreateServerless(host, user, pass, db);

	char params[1][64];
	Arg1(args, params[0], sizeof(params[]));
	if (!params[0][0])
		Source2ModUtils.Copy(params[0], sizeof(params[]), "hello");

	g_Serverless.QueryWithParams("SELECT ? AS greeting, NOW() AS now", params, 1, OnMySQLGreeting, SlotOf(context));
}

public void OnMySQLGreeting(bool success, int errorCode, const char[] error, ISource2ModSQLQuery query, any slot)
{
	if (!success)
	{
		ReplyToSlot(slot, "MySQL error %d: %s", errorCode, error);
		return;
	}

	ISource2ModSQLResult rows = query != null ? query.GetResultSet() : null;
	if (rows != null && rows.FetchRow())
	{
		char greeting[64], now[64];
		rows.GetString(0, greeting, sizeof(greeting));
		rows.GetString(1, now, sizeof(now));
		ReplyToSlot(slot, "MySQL says %s at %s", greeting, now);
	}
}

public void Cmd_MySQLTxn(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	char host[64], user[64], pass[64], db[64];
	if (!MySQLSettings(context, host, user, pass, db))
		return;

	if (g_Connection == null)
		g_Connection = Source2ModSQL.CreateConnection(host, user, pass, db);

	g_Connection.Connect(OnMySQLConnected, SlotOf(context));
}

public void OnMySQLConnected(bool connected, any slot)
{
	if (!connected)
	{
		ReplyToSlot(slot, "Could not connect.");
		return;
	}

	// ExecuteTransaction runs a copy: the transaction can go right after.
	Source2ModSQLTransaction txn = new Source2ModSQLTransaction();
	txn.AddQuery("SELECT VERSION()");
	txn.AddQuery("SELECT COUNT(*) FROM information_schema.tables");
	g_Connection.ExecuteTransaction(txn, OnTxnSuccess, OnTxnFailure, slot);
	delete txn;
}

public void OnTxnSuccess(ArrayList queries, any slot)
{
	// One result per query, in order.
	ISource2ModSQLQuery versionQuery = queries.Get(0);
	ISource2ModSQLQuery tablesQuery = queries.Get(1);
	ISource2ModSQLResult version = versionQuery.GetResultSet();
	ISource2ModSQLResult tables = tablesQuery.GetResultSet();

	if (version != null && version.FetchRow() && tables != null && tables.FetchRow())
	{
		char text[64];
		version.GetString(0, text, sizeof(text));
		ReplyToSlot(slot, "MySQL %s, %d tables", text, tables.GetInt(0));
	}
}

public void OnTxnFailure(const char[] error, int failIndex, any slot)
{
	ReplyToSlot(slot, "Transaction failed at query %d: %s", failIndex, error);
}

/* ============================================================================
 *
 *   18. Menus and custom HUD
 *
 *   A center HTML menu picked with the slot keys, and clicks on the buttons
 *   of a custom HUD layout (a custom_hud_layout entity a map or a plugin
 *   placed).
 *
 * ========================================================================== */

// Built once, the script's until deleted; deleting it would close it for
// everyone who has it open. It goes with the plugin.
CenterHtmlMenu g_Menu;

void SetupMenus()
{
	g_Menu = new CenterHtmlMenu("sp_sample menu");
	g_Menu.AddMenuOption("Heal to 100", Menu_Heal);
	g_Menu.AddMenuOption("Give an AK-47", Menu_GiveAk);

	// Shown, but cannot be picked.
	g_Menu.AddMenuOption("Not today", Menu_Heal, 0, true);

	// What happens after a pick: close, go back to the first page, or stay.
	g_Menu.PostSelect = PostSelect_Close;

	Source2ModCommands.RegisterConCommand("sp_menu", Cmd_Menu);

	// sp_hud: hooks the button clicks of every custom HUD layout on the map.
	// The hook goes with the layout entity, or with the plugin.
	Source2ModCommands.RegisterConCommand("sp_hud", Cmd_Hud);
}

public void Menu_Heal(CCSPlayerController player, CenterHtmlMenu menu, int option, any data)
{
	CCSPlayerPawn pawn = player.GetPlayerPawn();
	if (pawn != NULL_ENTITY && player.m_bPawnIsAlive)
		pawn.m_iHealth = 100;
}

public void Menu_GiveAk(CCSPlayerController player, CenterHtmlMenu menu, int option, any data)
{
	CCSPlayerPawn pawn = player.GetPlayerPawn();
	if (pawn == NULL_ENTITY || !player.m_bPawnIsAlive)
		return;

	CPlayer_ItemServices items = pawn.m_pItemServices;
	if (!items.IsNull)
		items.GiveNamedItem("weapon_ak47");
}

public void Cmd_Menu(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	CCSPlayerController caller = CallerOf(context);
	if (caller != NULL_ENTITY)
		Source2ModMenus.OpenCenterHtmlMenu(caller, g_Menu);
	else
		Reply(context, "Menus are for players.");
}

public void Cmd_Hud(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	int hooked = 0;

	CBaseEntity entity = NULL_ENTITY;
	while ((entity = Source2ModEntities.FindEntityByClassname(entity, "custom_hud_layout")) != NULL_ENTITY)
	{
		Source2ModCustomHud.HookCustomHudClick(view_as<CCSCustomHudLayout>(entity), OnHudClick);
		hooked++;
	}

	Reply(context, "Hooked %d custom HUD layout(s).", hooked);
}

public void OnHudClick(CCSPlayerController player, CCSCustomHudLayout layout, const char[] buttonId)
{
	char name[64] = "?";
	if (player != NULL_ENTITY)
		player.GetPlayerName(name, sizeof(name));

	Source2ModApi.Log("%s clicked \"%s\" on a custom HUD layout", name, buttonId);
}

/* ============================================================================
 *
 *   19. Scripts
 *
 *   CS2's map logic is JavaScript (cs_script, point_script entities). A
 *   plugin can run scripts of its own, fire inputs into them and hear what
 *   they send back through "source2mod_script <channel> <payload>".
 *
 * ========================================================================== */

void SetupScripts()
{
	// Everything a script sends on "sp_sample", a frame or more later.
	Source2ModScripts.HookScriptMessage("sp_sample", OnScriptMessage);

	// sp_script: runs the source below as a point_script named
	// sp_sample_script; running it again replaces it (a hot reload).
	Source2ModCommands.RegisterConCommand("sp_script", Cmd_Script);

	// sp_script_ping: native -> script; the script answers "pong".
	Source2ModCommands.RegisterConCommand("sp_script_ping", Cmd_ScriptPing);
}

bool IsHexDigit(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

public void OnScriptMessage(const char[] channel, const char[] payload)
{
	// The script encodes its payload (encodeURIComponent) so that a ';' or a
	// newline cannot split the command line; undo that here.
	char text[512], hex[3];
	int length = 0;
	for (int i = 0; payload[i] != '\0' && length < sizeof(text) - 1; i++)
	{
		if (payload[i] == '%' && IsHexDigit(payload[i + 1]) && IsHexDigit(payload[i + 2]))
		{
			hex[0] = payload[i + 1];
			hex[1] = payload[i + 2];
			hex[2] = '\0';
			text[length++] = view_as<char>(Source2ModUtils.StringToInt(hex, 16));
			i += 2;
		}
		else
		{
			text[length++] = payload[i];
		}
	}
	text[length] = '\0';

	Source2ModApi.Log("Script message on %s: %s", channel, text);
}

public void Cmd_Script(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	if (!Source2ModScripts.CanRunSource())
	{
		Reply(context, "This server cannot compile cs_script source.");
		return;
	}

	char source[512];
	Source2ModApi.Format(source, sizeof(source), "%s\n%s\n%s\n",
	       "import { Instance } from \"cs_script/point_script\";",
	       "Instance.ServerCommand(\"source2mod_script sp_sample \" + encodeURIComponent(\"hello from cs_script\"));",
	       "Instance.OnScriptInput(\"ping\", () => Instance.ServerCommand(\"source2mod_script sp_sample pong\"));");

	CCSPointScriptEntity script = Source2ModScripts.RunSource("sp_sample_script", "scripts/sp_sample_script.vjs", source);
	Reply(context, "sp_sample_script: %s", script != NULL_ENTITY ? "running" : "failed");
}

public void Cmd_ScriptPing(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
	if (!Source2ModScripts.FireInput("sp_sample_script", "ping"))
		Reply(context, "Run sp_script first.");
}
