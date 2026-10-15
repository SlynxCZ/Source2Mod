/**
 * vim: set ts=4 sw=4 tw=99 noet :
 * ======================================================
 * Source2Mod C++ Sample Plugin
 * ======================================================
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from
 * the use of this software.
 *
 * This sample plugin is public domain.
 */

#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <vector>
#include "plugin.h"

#include "engine2/networkgameserverbase.h"
#include "engine2/inetworkserverservice.h"
#include "engine2/isource2server.h"
#include "engine2/ivengineserver2.h"

#include "source2mod/schema/entity/classes/CCSPlayerController.h"
#include "source2mod/schema/entity/classes/CCSPlayerController_InGameMoneyServices.h"
#include "source2mod/schema/entity/classes/CPlayer_ItemServices.h"
#include "source2mod/schema/entity/classes/CCSGameRules.h"
#include "source2mod/schema/entity/classes/CSmokeGrenadeProjectile.h"
#include "source2mod/schema/entity/classes/CCSCustomHudLayout.h"

// The game's protobufs are part of the SDK: message ids (UM_Shake,
// clc_VoiceData) and, for anyone who prefers them over reflection, the
// generated message classes themselves.
#include "usermessages.pb.h"
#include "netmessages.pb.h"

// Section 12: not part of SOURCE2MOD_SAVEVARS(), asked for by name. Brings the
// SourcePawn headers (sp_vm_api.h) with it.
#include "source2mod/ISource2ModNatives.h"

SamplePlugin g_Plugin;
SOURCE2MOD_EXPOSE(cpp_sample, g_Plugin);

ISource2ModModule *g_pServerModule = nullptr;

/* ============================================================================
 *
 *   Shared helpers
 *
 * ========================================================================== */

// Who ran the command: a controller for a player, nullptr for the server
// console -- the console has no slot.
static CCSPlayerController *CallerOf(const Source2ModCommandContext &context)
{
    const CPlayerSlot slot = context.GetPlayerSlot();
    return slot.IsValid() ? CCSPlayerController::FromSlot(slot) : nullptr;
}

// Answers where the command came from: the player's chat, or the server console.
static void Reply(const Source2ModCommandContext &context, const char *pszFormat, ...)
{
    char szBuffer[512];

    va_list args;
    va_start(args, pszFormat);
    vsnprintf(szBuffer, sizeof(szBuffer), pszFormat, args);
    va_end(args);

    if (CCSPlayerController *pCaller = CallerOf(context))
        pCaller->PrintToChat(szBuffer);
    else
        SOURCE2MOD_CONPRINTF("%s\n", szBuffer);
}

// Most of the entity commands need a living caller. Says why when there is none.
static CCSPlayerPawn *AlivePawnOf(const Source2ModCommandContext &context)
{
    CCSPlayerController *pCaller = CallerOf(context);
    if (!pCaller)
    {
        Reply(context, "This command is for players, not for the server console.");
        return nullptr;
    }

    CCSPlayerPawn *pPawn = pCaller->GetPlayerPawn();
    if (!pPawn || !pCaller->m_bPawnIsAlive())
    {
        Reply(context, "You have to be alive for this.");
        return nullptr;
    }

    return pPawn;
}

template <typename FN>
static void ForEachPlayer(FN &&fn)
{
    for (int i = 0; i < 64; i++)
    {
        if (CCSPlayerController *pController = CCSPlayerController::FromSlot(i))
            fn(pController);
    }
}

/* ============================================================================
 *
 *   1. Lifecycle
 *
 *   Load() is the one entry point: save the globals, fetch the engine
 *   interfaces, install hooks, then let each section register its own things.
 *   Unload() undoes what Source2Mod cannot undo for you.
 *
 * ========================================================================== */

bool SamplePlugin::Load(PluginId id, ISource2ModAPI* api, char* error, size_t maxlen, bool late)
{
    SOURCE2MOD_SAVEVARS();

    g_pSource2Server = api->GetSource2Server();
    g_pSource2GameClients = api->GetSource2GameClients();
    g_pEngineServer = api->GetEngineServer();
    g_pCVar = api->GetCVar();

    SOURCE2MOD_LOG(this, "Starting plugin.\n");

    // For the hardcoded pattern in plugin.h: FindPattern needs the module.
    g_pServerModule = LOAD_MODULE("server");

    // The two raw hooks declared with KHOOK_* in plugin.h go onto the
    // interface instance just set above. One that cannot be resolved is
    // logged and skipped; the rest still go in. The KHook commit check inside
    // returns false out of Load() on a mismatch -- only a plugin with raw
    // hooks has one to fail.
    KHOOK_INIT();

    SOURCE2MOD_LOG(this, "All hooks started!\n");

    // OnLevelInit, the client callbacks, OnGameFrame and the rest of
    // ISource2ModListener.
    api->AddListener(this, this);

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

    return true;
}

bool SamplePlugin::Unload(char* error, size_t maxlen)
{
    // Detaches every hook and deletes it -- deleting is what takes a detour
    // down. After this nothing in the engine points into this library any
    // more. A live hook left past unload would jump into unmapped memory on
    // the next call.
    KHOOK_DESTRUCT();

    if (g_pServerModule)
    {
        FREE_MODULE(g_pServerModule);
        g_pServerModule = nullptr;
    }

    // The CConVars below are objects in this library, and ConVar_Register handed
    // the engine pointers to them. Without this the engine keeps those pointers
    // after the library is unloaded, and the next thing to touch one -- the
    // console autocompleting, a config exec -- reads freed memory.
    //
    // ConVar_Unregister only walks the list held by the caller's own library, so
    // Source2Mod cannot do this on a plugin's behalf. Every plugin that
    // declares a CConVar has to make this call.
    ConVar_Unregister();

    // Everything else this plugin registered -- commands, command and chat
    // listeners, game event hooks, game hooks, net message hooks, the sound
    // hook, the entity listeners, timers, sounds -- belongs to it (the core
    // reads that off each handler), and Source2Mod drops all of it when the
    // plugin goes. Taking them down by hand (UnhookGameEvent, UnhookTakeDamage,
    // UnregisterConCommand, KillTimer, ... by the id or the same handler) is
    // for turning a feature off while the plugin stays loaded.

    return true;
}

void SamplePlugin::OnAllSource2ModPluginsLoaded()
{
    /* This is where we'd do stuff that relies on the mod or other plugins
     * being initialized (for example, cvars added and events registered).
     */
}

void SamplePlugin::OnLevelInit(const char* pMapName, const char* pMapEntities, const char* pOldLevel, const char* pLandmarkName, bool loadGame, bool background)
{
    SOURCE2MOD_LOG(this, "OnLevelInit(%s)\n", pMapName);
}

void SamplePlugin::OnLevelShutdown()
{
    SOURCE2MOD_LOG(this, "OnLevelShutdown()\n");

    // The countdown timer is TIMER_FLAG_NO_MAPCHANGE: the scheduler kills it
    // here, and the pointer with it. See SetupTimers().
    m_pCountdownTimer = nullptr;
}

/* ============================================================================
 *
 *   2. ConVars
 *
 *   Declaring your own, reading and writing the game's, and hearing about
 *   changes.
 *
 * ========================================================================== */

CConVar<int> sample_cvari("sample_cvari", FCVAR_NONE, "help string", 42);
CConVar<float> sample_cvarf("sample_cvarf", FCVAR_NONE, "help string", 69.69f, true, 10.0f, true, 100.0f);

// Used by the later sections, so that each of them can be tried from the console.
CConVar<bool> sample_hide_bot_deaths("sample_hide_bot_deaths", FCVAR_NONE, "Keep bot deaths out of the killfeed (4. game events)", false);
CConVar<float> sample_shake_max("sample_shake_max", FCVAR_NONE, "Largest screen shake amplitude a client is sent (6. net messages)", 25.0f, true, 0.0f, false, 0.0f);
CConVar<float> sample_damage_scale("sample_damage_scale", FCVAR_NONE, "Multiplier for all damage (8. native functions)", 1.0f, true, 0.0f, false, 0.0f);

void SamplePlugin::SetupConVars()
{
    // The engine only takes plain function pointers for this, and keeps one
    // list of them for the whole process. Source2Mod installs a single one and
    // fans out, so a capturing lambda works here.
    g_pSource2ModConVars->HookConVarChange([](ConVarRefAbstract *ref, CSplitScreenSlot slot, const char *pszNewValue, const char *pszOldValue)
    {
        if (V_strcmp(ref->GetName(), "sample_cvarf") != 0)
            return;

        SOURCE2MOD_LOG(&g_Plugin, "Sample convar \"%s\" was changed from %s to %s\n", ref->GetName(), pszOldValue, pszNewValue);
    });

    ConVar_Register(FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE | FCVAR_GAMEDLL);

    // You can get a convar reference to an already existing cvar via CConVarRef.
    // This will pre-register it if it's not yet registered and would use default
    // data until the actual cvar is registered. You can assert data existance via
    // IsConVarDataAvailable(). Make sure the type is correct here otherwise it
    // might prevent actual convar being registered, since you pre-registered it
    // with a different type or if convar already exists you'd be left with an
    // invalid ref, so a check for IsValidRef() is also nice to have.
    //
    // Side Note: Always make sure you are working with a valid ref (IsValidRef())
    // before reading/writing to it as otherwise you'd be reading/writing off of
    // default convar data which is shared across all the invalid convar refs.
    CConVarRef<int> ccvar_ref_example("mp_limitteams");

    if (ccvar_ref_example.IsValidRef() && ccvar_ref_example.IsConVarDataAvailable())
    {
        SOURCE2MOD_LOG(this, "CConVarRef \"%s\" got value pre = %d [float = %f, bool = %d, string = \"%s\"]\n",
                    ccvar_ref_example.GetName(), ccvar_ref_example.Get(), ccvar_ref_example.GetFloat(),
                    ccvar_ref_example.GetBool(), ccvar_ref_example.GetString().Get());

        // By default if you are using CConVar or CConVarRef you should be using
        // Get()/Set() methods to read/write values, as these are templated for the
        // particular type the cvar is of. It also is usually faster since it skips
        // all the type conversion logic of non templated methods
        ccvar_ref_example.Set(5);

        // As noted above there are methods that support value conversion between
        // certain types so stuff like this is possible on an int typed cvar for
        // example, refer to ConVarRefAbstract declaration for more info on these
        ccvar_ref_example.SetFloat(8.5f);

        SOURCE2MOD_LOG(this, "CConVarRef \"%s\" got value after = %d [float = %f, bool = %d, string = \"%s\"]\n",
                    ccvar_ref_example.GetName(), ccvar_ref_example.Get(), ccvar_ref_example.GetFloat(),
                    ccvar_ref_example.GetBool(), ccvar_ref_example.GetString().Get());
    }

    // You can also use ConVarRefAbstract class if you don't want typisation
    // support or don't know the actual type used, since you are responsible for
    // picking the correct type there! And ConVarRefAbstract won't pre-register the
    // convar in the system, as it acts as a plain ref, so make sure to check the
    // ref for validity before usage via IsValidRef()
    ConVarRefAbstract cvar_ref_example("mp_limitteams");

    if (cvar_ref_example.IsValidRef())
    {
        SOURCE2MOD_LOG(this, "ConVarRefAbstract \"%s\" got value pre [float = %f, bool = %d, string = \"%s\"]\n",
                    cvar_ref_example.GetName(), cvar_ref_example.GetFloat(), cvar_ref_example.GetBool(),
                    cvar_ref_example.GetString().Get());

        // Since the ref is not typed, you can't use direct Get() and Set() methods,
        // instead you need to use methods with type conversion support.
        cvar_ref_example.SetFloat(10.0f);

        // If you work with convars of non primitive types, you can also use SetAs()
        // methods to try to set the value as a specific type, if type mismatches it
        // would try to do conversion if possible and if not it would do nothing.
        // There's also an equvialent methods for reading the value, GetAs()
        cvar_ref_example.SetAs<Vector>(Vector(1.0f, 2.0f, 3.0f));

        // Alternatively you can "promote" plain ref to a typed variant by passing
        // plain ref to a constructor but be careful, as there's a type checker in
        // place that would invalidate convar ref if cast to a wrong type was
        // attempted, you can check for that either via IsValidRef() or
        // IsConVarDataValid() afterwards, but generally you should just know the
        // correct type of the cvar you are casting to beforehand.
        CConVarRef<int> promoted_ref(cvar_ref_example);
        if (promoted_ref.IsValidRef() && promoted_ref.IsConVarDataValid())
        {
            promoted_ref.Set(5);
        }

        SOURCE2MOD_LOG(this, "ConVarRefAbstract \"%s\" got value after [float = %f, bool = %d, string = \"%s\"]\n",
                    cvar_ref_example.GetName(), cvar_ref_example.GetFloat(), cvar_ref_example.GetBool(),
                    cvar_ref_example.GetString().Get());
    }
}

/* ============================================================================
 *
 *   3. Commands
 *
 *   A command of your own, its arguments and its caller, a chat-only
 *   trigger, and listening in on a command that is not yours.
 *
 * ========================================================================== */

void SamplePlugin::SetupCommands()
{
    // A console command, and the same thing again as a chat command -- the
    // source2mod routes "!sample_command" and "/sample_command" to the same handler.
    g_pSource2ModCommands->RegisterConCommand("sample_command", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        SOURCE2MOD_LOG(&g_Plugin, "Sample command called by %d. Command: %s\n", context.GetPlayerSlot().Get(), args.GetCommandString());
    });

    // Arguments and the caller. Arg(0) is the command itself, ArgS() is
    // everything after it as one string. The caller is a slot: valid for a
    // player, invalid (-1) for the server console -- CallerOf() and Reply()
    // at the top of this file are built on that.
    g_pSource2ModCommands->RegisterConCommand("sample_echo", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        if (args.ArgC() < 2)
        {
            Reply(context, "Usage: sample_echo <text>");
            return;
        }

        CCSPlayerController *pCaller = CallerOf(context);
        Reply(context, "%s said: %s (%d arguments)", pCaller ? pCaller->GetPlayerName() : "Console", args.ArgS(), args.ArgC() - 1);
    });

    // A chat listener has no console command behind it: it fires when the word
    // is typed into chat after a chat trigger -- "!noshake" or "/noshake" with
    // the default triggers; plain "noshake" does not fire it. The handler
    // cannot block the message itself: a public trigger lets it show, a
    // silent one hides it whatever the handler does.
    g_pSource2ModCommands->RegisterChatListener("noshake", [this](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        const CPlayerSlot slot = context.GetPlayerSlot();
        if (!slot.IsValid())
            return;

        // Used by the outgoing net message hook in section 6.
        m_NoShakeMask ^= (uint64_t(1) << slot.Get());

        const bool bOff = (m_NoShakeMask >> slot.Get()) & 1;
        Reply(context, "Screen shakes are now %s for you.", bOff ? "off" : "on");
    });

    // Listening in on a command somebody else owns -- the game's, another
    // plugin's. Pre (post = false) may answer with Action::Supersede to stop
    // the command from running at all; post only watches.
    g_pSource2ModCommands->RegisterConListener("jointeam", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post) -> Action
    {
        // "jointeam 1" is spectator. Refusing it here is the whole of a
        // "no spectators" plugin.
        if (args.ArgC() >= 2 && atoi(args.Arg(1)) == 1 && sample_cvari.Get() == 1337)
        {
            Reply(context, "Spectating is off while sample_cvari is 1337.");
            return Action::Supersede;
        }

        return Action::Ignore;
    }, false);

    g_pSource2ModCommands->RegisterConListener("jointeam", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post) -> Action
    {
        SOURCE2MOD_LOG(&g_Plugin, "Slot %d ran \"%s\"\n", context.GetPlayerSlot().Get(), args.GetCommandString());
        return Action::Ignore;
    }, true);
}

/* ============================================================================
 *
 *   4. Game events
 *
 *   Hooking before and after the engine handles an event, reading and
 *   changing its fields, keeping it from the clients, and firing one.
 *
 * ========================================================================== */

void SamplePlugin::SetupGameEvents()
{
    // Pre (post = false): the event has been created but nobody has seen it
    // yet. Fields may be changed, dontBroadcast keeps it on the server, and
    // Action::Supersede cancels it outright.
    g_pSource2ModEvents->HookGameEvent("player_death", [](IGameEvent *event, bool post, bool &dontBroadcast) -> Action
    {
        // Player fields resolve straight to the entity, the slot or the pawn.
        auto *pVictim = static_cast<CCSPlayerController *>(event->GetPlayerController("userid"));
        auto *pAttacker = static_cast<CCSPlayerController *>(event->GetPlayerController("attacker"));

        SOURCE2MOD_LOG(&g_Plugin, "player_death: %s killed %s with %s%s\n",
                    pAttacker ? pAttacker->GetPlayerName() : "world",
                    pVictim ? pVictim->GetPlayerName() : "?",
                    event->GetString("weapon"),
                    event->GetBool("headshot") ? " (headshot)" : "");

        // The server still processes the event -- stats, other plugins' post
        // hooks -- but no client is told, so the killfeed never shows it.
        if (sample_hide_bot_deaths.Get() && pVictim && pVictim->IsBot())
        {
            dontBroadcast = true;
            return Action::Override;
        }

        return Action::Ignore;
    }, false);

    // Post (post = true): the engine is done with it. The place to react.
    // This one gives everybody $1337 at the start of each round.
    g_pSource2ModEvents->HookGameEvent("round_start", [](IGameEvent *event, bool post, bool &dontBroadcast) -> Action
    {
        ForEachPlayer([](CCSPlayerController *pController)
        {
            if (auto *pMoney = pController->m_pInGameMoneyServices())
                pMoney->m_iAccount = 1337;
        });

        return Action::Ignore;
    }, true);

    g_pSource2ModEvents->HookGameEvent("player_jump", [](IGameEvent *event, bool post, bool &dontBroadcast) -> Action
    {
        SOURCE2MOD_LOG(&g_Plugin, "player_jump: slot %d\n", event->GetPlayerSlot("userid").Get());
        return Action::Ignore;
    }, true);

    // Firing an event. Created through the engine's event manager, filled in,
    // then sent one of two ways:
    //
    //   * FireEvent()         -- to the server and every client, through every hook;
    //   * FireEventToClient() -- to one player only, which the server itself
    //                            never sees. That is what makes per-player HUD
    //                            text out of show_survival_respawn_status.
    g_pSource2ModCommands->RegisterConCommand("sample_event", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        CCSPlayerController *pCaller = CallerOf(context);
        if (!pCaller)
        {
            Reply(context, "This command is for players.");
            return;
        }

        IGameEventManager2 *pEventManager = g_Source2ModAPI->GetGameEventManager();

        IGameEvent *pEvent = pEventManager->CreateEvent("show_survival_respawn_status", true);
        if (!pEvent)
            return;

        pEvent->SetString("loc_token", "<font color='#00ff00'>Hello from a game event</font>");
        pEvent->SetInt("duration", 5);
        pEvent->SetPlayer("userid", pCaller->GetPlayerSlot());

        pCaller->FireEventToClient(pEvent);

        // FireEvent() frees the event it is given; an event that went out any
        // other way is still yours.
        pEventManager->FreeEvent(pEvent);
    });
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

void SamplePlugin::SetupCoreEvents()
{
    // IEntityListener: OnEntityCreated / Spawned / Deleted / ParentChanged.
    g_pSource2ModEntities->AddEntityListener(this);

    // IEntityIOListener: a classname and an output, nullptr for "any". Pre
    // may return Action::Supersede, and the output then never fires -- the
    // door stays shut, the button does nothing.
    g_pSource2ModEntities->AddEntityIOListener(this, "func_button", "OnPressed");
    g_pSource2ModEntities->AddEntityIOListener(this, nullptr, "OnStartTouch", true);

    // The client callbacks and OnGameFrame come through ISource2ModListener, see
    // below; two engine calls Source2Mod has no callback for are raw KHook
    // hooks.
}

void SamplePlugin::OnEntityCreated(CEntityInstance* pEntity)
{
    // Created is early: the entity exists, its keyvalues have not been applied
    // and it has not spawned. Good for noticing, too soon for changing.
}

void SamplePlugin::OnEntitySpawned(CEntityInstance* pEntity)
{
    // Every smoke grenade thrown from here on is purple.
    if (V_strcmp(pEntity->GetClassname(), "smokegrenade_projectile") != 0)
        return;

    // The game fills the projectile in right after spawning it, so a change
    // made here would be overwritten. One frame later it sticks. A raw pointer
    // must not cross a frame -- the entity may be gone by then -- a handle
    // resolves to nullptr instead.
    CHandle<CSmokeGrenadeProjectile> hSmoke = static_cast<CSmokeGrenadeProjectile *>(pEntity)->GetHandle();

    g_pSource2ModScheduler->NextFrame([hSmoke]()
    {
        if (CSmokeGrenadeProjectile *pSmoke = hSmoke.Get())
            pSmoke->m_vSmokeColor = Vector(170.0f, 0.0f, 255.0f);
    });
}

void SamplePlugin::OnEntityDeleted(CEntityInstance* pEntity)
{
    // Last call for this entity. Anything holding a raw pointer to it has to
    // let go now.
}

Action SamplePlugin::OnEntityOutput(const char* pchOutputName, CEntityInstance* pActivator, CEntityInstance* pCaller, float flDelay, bool post)
{
    // OnStartTouch fires constantly; only the button is worth a log line.
    if (!post)
    {
        SOURCE2MOD_LOG(this, "%s fired %s (activator: %s)\n",
                    pCaller ? pCaller->GetClassname() : "?", pchOutputName,
                    pActivator ? pActivator->GetClassname() : "none");
    }

    return Action::Ignore;
}

// The client and frame callbacks come from ISource2ModListener, the same object
// Load() handed to AddListener(). Nothing to install or take down.

void SamplePlugin::OnClientPutInServer(CPlayerSlot slot, const char* pszName, int type, uint64 xuid)
{
    SOURCE2MOD_LOG(this, "OnClientPutInServer(%d, \"%s\", %d, %lld)\n", slot.Get(), pszName, type, xuid);
}

void SamplePlugin::OnClientSettingsChanged(CPlayerSlot slot)
{
    SOURCE2MOD_LOG(this, "OnClientSettingsChanged(%d)\n", slot.Get());
}

void SamplePlugin::OnClientDisconnect(CPlayerSlot slot, ENetworkDisconnectionReason reason, const char* pszName, uint64 xuid, const char* pszNetworkID)
{
    SOURCE2MOD_LOG(this, "OnClientDisconnect(%d, %d, \"%s\", %lld, \"%s\")\n", slot.Get(), reason, pszName, xuid, pszNetworkID);

    // Per-slot state has to go with the player, or the next one to take the
    // slot inherits it.
    const uint64_t bit = uint64_t(1) << slot.Get();
    m_NoShakeMask &= ~bit;
    m_VoiceMutedMask &= ~bit;
}

void SamplePlugin::OnGameFrame(bool simulating, bool bFirstTick, bool bLastTick)
{
    // Runs every tick, right after the game's own GameFrame -- 64 times a
    // second, so keep it cheap. `simulating` is false while the game is not
    // ticking (paused, hibernating).
}

// Two things Source2Mod has no callback for: turning a connection away, and
// swallowing a client command before the engine sees it. For those the
// plugin hooks the engine itself -- the KHOOK_VIRTUAL lines in plugin.h,
// installed by KHOOK_INIT() in Load(). See there for what that costs.

KHook::Return<bool> SamplePlugin::Hook_ClientConnect(ISource2GameClients* pThis, CPlayerSlot slot, const char* pszName, uint64 xuid, const char* pszNetworkID, bool unk1, CBufferString* pRejectReason)
{
    SOURCE2MOD_LOG(this, "Hook_ClientConnect(%d, \"%s\", %lld, \"%s\", %d, \"%s\")\n", slot.Get(), pszName, xuid, pszNetworkID, unk1, pRejectReason->Get());

    // A hook on a function that returns something returns the value alongside
    // the action. With Override the original still runs but the value below is
    // what the caller gets -- here: refuse the connection and hand back the
    // reason. Try it by connecting with "sample_rejected" as your name.
    if (!V_strcmp(pszName, "sample_rejected"))
    {
        pRejectReason->Insert(0, "Rejected by the sample plugin.");
        return { KHook::Action::Override, false };
    }

    // Ignore with a value still returns whatever the original returns -- the
    // value passed here is discarded. Only Supersede/Override make it count.
    return { KHook::Action::Ignore, true };
}

KHook::Return<void> SamplePlugin::Hook_ClientCommand(ISource2GameClients* pThis, CPlayerSlot slot, const CCommand& args)
{
    // Supersede blocks the original entirely -- the engine never sees this
    // command. Use it to take a command over, not to "handle it as well".
    if (!V_strcmp(args.Arg(0), "sample_blocked"))
    {
        SOURCE2MOD_LOG(this, "Swallowing \"sample_blocked\" -- the engine will never see it.\n");
        return { KHook::Action::Supersede };
    }

    // Ignore says "I did nothing": the original runs, and so does whatever else
    // is hooked here. The right answer for a hook that only looks.
    return { KHook::Action::Ignore };
}

/* ============================================================================
 *
 *   6. Net messages
 *
 *   Building a protobuf message and sending it to chosen players, and sitting
 *   on the wire in both directions.
 *
 * ========================================================================== */

void SamplePlugin::SetupNetMessages()
{
    // Sending. Allocate by id (or by name, NET_MSG_ALLOC_BY_NAME), set fields
    // by their .proto names, send, free. The message is yours throughout:
    // sending does not consume it, so one message can go out many times.
    g_pSource2ModCommands->RegisterConCommand("sample_shake", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        void *pMsg = NET_MSG_ALLOC_BY_ID(UM_Shake);
        if (!pMsg)
            return;

        // CUserMessageShake, usermessages.proto
        g_pSource2ModNetworkMessages->SetUInt32(pMsg, "command", 0); // SHAKE_START
        g_pSource2ModNetworkMessages->SetFloat(pMsg, "amplitude", args.ArgC() >= 2 ? strtof(args.Arg(1), nullptr) : 10.0f);
        g_pSource2ModNetworkMessages->SetFloat(pMsg, "frequency", 150.0f);
        g_pSource2ModNetworkMessages->SetFloat(pMsg, "duration", 2.0f);

        const CPlayerSlot slot = context.GetPlayerSlot();
        if (slot.IsValid())
            NET_MSG_SEND(pMsg, UM_Shake, slot);             // the caller only
        else
            NET_MSG_SEND_PLAYERS(pMsg, UM_Shake, ~0ull);    // from the console: everybody

        NET_MSG_FREE(pMsg);
    });

    // Server -> clients. Every message the server posts, this plugin's own
    // included, with the recipients as a bitmask that may be edited. The
    // message may be edited too: the clients get what is left when the hooks
    // are done. Action::Supersede drops it for everybody.
    g_pSource2ModNetworkMessages->HookServerMessage([this](uint64_t *clients, int messageid, void *msg) -> Action
    {
        if (messageid != UM_Shake)
            return Action::Ignore;

        // Changing a field: no shake stronger than sample_shake_max.
        const float flMax = sample_shake_max.Get();
        if (g_pSource2ModNetworkMessages->GetFloat(msg, "amplitude") > flMax)
            g_pSource2ModNetworkMessages->SetFloat(msg, "amplitude", flMax);

        // Changing the recipients: not to those who typed !noshake.
        *clients &= ~m_NoShakeMask;

        // The handle is the protobuf itself, for those who would rather use
        // the generated class than field names:
        //
        //     auto *pShake = static_cast<CUserMessageShake *>(static_cast<google::protobuf::Message *>(msg));
        //     pShake->set_amplitude(flMax);

        return Action::Ignore;
    });

    // Client -> server. Here: a voice mute. The packets of a muted player are
    // dropped before the server looks at them, so nobody hears them.
    g_pSource2ModNetworkMessages->HookClientMessage([this](CPlayerSlot slot, int messageid, void *msg) -> Action
    {
        if (messageid == clc_VoiceData && ((m_VoiceMutedMask >> slot.Get()) & 1))
            return Action::Supersede;

        return Action::Ignore;
    });

    g_pSource2ModCommands->RegisterConCommand("sample_mute", [this](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        if (args.ArgC() < 2)
        {
            Reply(context, "Usage: sample_mute <slot>");
            return;
        }

        const int iSlot = atoi(args.Arg(1));
        CCSPlayerController *pTarget = (iSlot >= 0 && iSlot < 64) ? CCSPlayerController::FromSlot(iSlot) : nullptr;
        if (!pTarget)
        {
            Reply(context, "Nobody in slot %d.", iSlot);
            return;
        }

        m_VoiceMutedMask ^= (uint64_t(1) << iSlot);
        Reply(context, "%s is now %s.", pTarget->GetPlayerName(), ((m_VoiceMutedMask >> iSlot) & 1) ? "muted" : "unmuted");
    });
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

void SamplePlugin::SetupSounds()
{
    // The short way: one call, one player, heard "in the head" -- no position.
    g_pSource2ModCommands->RegisterConCommand("sample_sound", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        const CPlayerSlot slot = context.GetPlayerSlot();
        const char *pszSound = args.ArgC() >= 2 ? args.Arg(1) : "Weapon_AK47.Single";

        if (slot.IsValid())
            SOUND_EMIT_TO_PLAYER(slot, pszSound);
        else
            SOUND_EMIT_TO_ALL(pszSound);
    });

    // The long way: a sound object. Source, volume, pitch, any other public
    // parameter of the sound event, and exactly who hears it.
    g_pSource2ModCommands->RegisterConCommand("sample_sound_world", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        CCSPlayerPawn *pPawn = AlivePawnOf(context);
        if (!pPawn)
            return;

        ISource2ModSound *pSound = ISource2ModSound::New("Weapon_AK47.Single");

        pSound->SetVolume(0.5f);
        pSound->SetPitch(2.0f);

        // One of three sources:
        //   SetSourceEntityIndex(SOUND_SOURCE_LISTENER) -- at each recipient (default)
        //   SetPosition(vec)                            -- a fixed point in the world
        //   SetSourceEntity(pEntity)                    -- from an entity, following it
        pSound->SetPosition(pPawn->GetAbsOrigin() + Vector(0.0f, 0.0f, 128.0f));

        // Parameters beyond volume and pitch go by the name the sound event
        // declares them under:
        //     pSound->SetFloat("public.my_param", 1.0f);

        pSound->AddAllRecipients();
        pSound->RemoveRecipient(CPlayerSlot(63)); // everybody but slot 63

        // Emit() may be called again and again; each call is a new sound with
        // a new guid. The guid is what stops it or changes it while it plays.
        const SoundGuid guid = pSound->Emit();
        delete pSound;

        ADD_TIMER(0.1f, [guid]() { g_pSource2ModSounds->SetSoundVolume(guid, 0.1f); });
        ADD_TIMER(0.3f, [guid]() { SOUND_STOP(guid); });
    });

    // The game's own sounds, on their way to the clients. Ignore leaves one
    // alone, Override sends it with the changes made here, Supersede drops it.
    g_pSource2ModSounds->HookSound([](ISource2ModSound *sound) -> Action
    {
        // Sounds travel as hashes. Hash the names you care about once and
        // compare numbers.
        static const uint32_t s_nKnifeHit = g_pSource2ModSounds->HashSoundName("Weapon_Knife.Hit");

        if (sound->GetNameHash() != s_nKnifeHit)
            return Action::Ignore;

        sound->SetPitch(1.5f);
        return Action::Override;
    });
}

/* ============================================================================
 *
 *   8. Game functions
 *
 *   Game functions no interface exposes. The common ones -- TakeDamage,
 *   CanAcquire, PostThink, the movement and jump functions, ... -- the core
 *   hooks for you: ISource2ModGameHooks, resolved from the core's own gamedata,
 *   so a plugin never touches a signature and survives an engine update with
 *   a core update. Anything else is hooked and called by signature.
 *
 * ========================================================================== */

void SamplePlugin::SetupGameFunctions()
{
    // A method as the handler: SOURCE2MOD_MEMBER (SourceHook's SH_MEMBER). The
    // same one handed to UnhookTakeDamage() takes it off again; a lambda goes
    // by the id HookTakeDamage() returns. false = Pre, before the game's code.
    g_pSource2ModGameHooks->HookTakeDamage(SOURCE2MOD_MEMBER(this, &SamplePlugin::OnTakeDamage), false);

    // A lambda, Post: runs after the game's PostThink, for every pawn every
    // tick -- do as little as possible here. A handler of a function without
    // a return value answers the action alone.
    g_pSource2ModGameHooks->HookPostThink([](PostThinkContext& ctx, bool post) -> Action
    {
        return Action::Ignore;
    }, true);

    // Calling. An address and a function pointer type are all it takes.
    g_pSource2ModCommands->RegisterConCommand("sample_swap", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        CCSPlayerController *pCaller = CallerOf(context);
        if (!pCaller || pCaller->m_iTeamNum() < 2)
        {
            Reply(context, "You have to be on a team.");
            return;
        }

        const unsigned char nNewTeam = pCaller->m_iTeamNum() == 2 ? 3 : 2;

        // Way one: Source2Mod resolved it and knows the type. ADDR_* getters
        // return a typed function pointer, ready to call.
        if (auto pfnSwitchTeam = ADDR_SWITCH_TEAM())
        {
            pfnSwitchTeam(pCaller, nNewTeam);
            return;
        }

        // Way two: any gamedata entry, with the type spelled out by you. Add
        // entries of your own to the gamedata and they resolve the same way.
        using SwitchTeam_t = void (FASTCALL *)(CCSPlayerController *, unsigned char);

        if (auto pfnSwitchTeam = GAMECONFIG_RESOLVE("CCSPlayerController::SwitchTeam").RCast<SwitchTeam_t>())
            pfnSwitchTeam(pCaller, nNewTeam);

        // For the common ones there is a third way, which is not to care:
        // pCaller->SwitchTeam(nNewTeam) does exactly the above.
    });
}

// The raw KHooks by signature (KHOOK_MEMBER / KHOOK_FUNCTION in plugin.h,
// one per way of getting the address).

// 1. ISource2ModAddresses, the virtual getter: every item a player is given --
// sample_give below, buying, a round's loadout. Pre.
KHook::Return<CBasePlayerWeapon*> SamplePlugin::Hook_GiveNamedItem(CCSPlayer_ItemServices* pThis, const char* pszItem, int nSubType, CEconItemView* pScriptItem, bool bForce, void* a6)
{
    SOURCE2MOD_LOG(this, "GiveNamedItem: %s\n", pszItem ? pszItem : "?");

    // { KHook::Action::Supersede, nullptr } would give nothing -- the caller
    // gets no weapon back.
    return { KHook::Action::Ignore, nullptr };
}

// 1. ISource2ModAddresses, the ADDR_* macro, on a free function: every entity
// the game removes. That is a lot of them, so it only looks at weapons.
KHook::Return<void> SamplePlugin::Hook_UtilRemove(CEntityInstance* pEntity)
{
    if (pEntity && !V_strncmp(pEntity->GetClassname(), "weapon_", 7))
        SOURCE2MOD_LOG(this, "UTIL_Remove: %s\n", pEntity->GetClassname());

    return { KHook::Action::Ignore };
}

// 3. The hardcoded pattern: a pawn's view snapped to new angles -- a
// teleport with angles, a respawn. Changing *pAngles here changes where it
// looks.
KHook::Return<void> SamplePlugin::Hook_SnapViewAngles(CBasePlayerPawn* pThis, QAngle* pAngles)
{
    // Bots have their views snapped all the time; only players are worth a
    // log line.
    auto *pController = pThis ? static_cast<CCSPlayerController *>(pThis->m_hController().Get()) : nullptr;
    if (!pController || pController->IsBot())
        return { KHook::Action::Ignore };

    if (pAngles)
        SOURCE2MOD_LOG(this, "SnapViewAngles: %.1f %.1f %.1f\n", pAngles->x, pAngles->y, pAngles->z);

    return { KHook::Action::Ignore };
}

// 2. The gamedata entry: every team switch, the one sample_swap makes above
// included. Pre, so the player is still on the old team here.
KHook::Return<void> SamplePlugin::Hook_SwitchTeam(CCSPlayerController* pThis, int nTeam)
{
    SOURCE2MOD_LOG(this, "SwitchTeam: %s %d -> %d\n", pThis ? pThis->GetPlayerName() : "?", pThis ? pThis->m_iTeamNum() : -1, nTeam);

    // { KHook::Action::Supersede } would keep the player where they are; the
    // original with other arguments is KHook::Recall(...), and the original
    // from the middle of the handler m_hSwitchTeam.CallOriginal(pThis, nTeam).
    return { KHook::Action::Ignore };
}

GameHookReturn<TakeDamageContext::Return> SamplePlugin::OnTakeDamage(TakeDamageContext& ctx, bool post)
{
    // The context holds the call: the entity taking damage, the damage info,
    // the result, and for a function with a return value ctx.result.
    SOURCE2MOD_LOG(this, "TakeDamage: %s, %.1f damage\n", ctx.entity ? ctx.entity->GetClassname() : "?", ctx.info ? ctx.info->m_flDamage : 0.0f);

    // Changing an argument: this is a Pre handler, so the game runs after it,
    // with whatever is in the damage info by then.
    if (ctx.info && sample_damage_scale.Get() != 1.0f)
        ctx.info->m_flDamage *= sample_damage_scale.Get();

    // Blocking it: the game's code does not run, the caller gets the value.
    //
    //     return { Action::Supersede, 0 };
    //
    // Knowing what the game would do, from Pre: CallOriginal() runs the game's
    // function now (past every hook) and returns what it returned. It really
    // runs, so answer Supersede with that value -- or it runs a second time:
    //
    //     const int64_t dealt = ctx.CallOriginal();
    //     ... ctx.damageResult is filled in now ...
    //     return { Action::Supersede, dealt };

    // Ignore: the game runs as it would have.
    return Action::Ignore;
}

/* ============================================================================
 *
 *   9. Entities
 *
 *   Schema fields read and written like members (a write to a networked
 *   field tells the engine to send it), the helpers on the schema classes,
 *   and finding, creating and poking entities.
 *
 * ========================================================================== */

void SamplePlugin::SetupEntityCommands()
{
    g_pSource2ModCommands->RegisterConCommand("sample_hp", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        CCSPlayerPawn *pPawn = AlivePawnOf(context);
        if (!pPawn)
            return;

        // Reading is a call, writing is an assignment. += -= |= and the rest
        // work too, and all of them mark the field as changed.
        const int nOld = pPawn->m_iHealth();
        pPawn->m_iHealth = args.ArgC() >= 2 ? atoi(args.Arg(1)) : 100;

        Reply(context, "Health: %d -> %d", nOld, pPawn->m_iHealth());
    });

    g_pSource2ModCommands->RegisterConCommand("sample_up", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        CCSPlayerPawn *pPawn = AlivePawnOf(context);
        if (!pPawn)
            return;

        // Position, angles, velocity; nullptr keeps what the entity has.
        const Vector vecTarget = pPawn->GetAbsOrigin() + Vector(0.0f, 0.0f, 100.0f);
        pPawn->Teleport(&vecTarget, nullptr, nullptr);
    });

    g_pSource2ModCommands->RegisterConCommand("sample_give", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        CCSPlayerPawn *pPawn = AlivePawnOf(context);
        if (!pPawn)
            return;

        // The services hang off the pawn as pointers, null on a pawn that does
        // not have them.
        CPlayer_ItemServices *pItems = pPawn->m_pItemServices();
        if (!pItems)
            return;

        const char *pszItem = args.ArgC() >= 2 ? args.Arg(1) : "weapon_ak47";
        if (!pItems->GiveNamedItem(pszItem))
            Reply(context, "Could not give \"%s\".", pszItem);
    });

    g_pSource2ModCommands->RegisterConCommand("sample_strip", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        if (CCSPlayerPawn *pPawn = AlivePawnOf(context))
        {
            if (CPlayer_ItemServices *pItems = pPawn->m_pItemServices())
                pItems->RemoveWeapons(false);
        }
    });

    g_pSource2ModCommands->RegisterConCommand("sample_slay", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        if (CCSPlayerPawn *pPawn = AlivePawnOf(context))
            pPawn->CommitSuicide(false, true);
    });

    g_pSource2ModCommands->RegisterConCommand("sample_respawn", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        if (CCSPlayerController *pCaller = CallerOf(context))
            pCaller->Respawn();
    });

    // Finding: by classname, one after another. nullptr starts the search,
    // the last match continues it.
    g_pSource2ModCommands->RegisterConCommand("sample_break", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        int nBroken = 0;

        CBaseEntity *pEntity = nullptr;
        while ((pEntity = FIND_ENTITY_BY_CLASSNAME(pEntity, "func_breakable")) != nullptr)
        {
            // An input, exactly as a map's I/O would send it. AddEntityIOEvent()
            // takes the same arguments plus a delay and queues it instead.
            pEntity->AcceptInput("Break");
            nBroken++;
        }

        Reply(context, "Broke %d func_breakable.", nBroken);
    });

    // What is under the crosshair.
    g_pSource2ModCommands->RegisterConCommand("sample_aim", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        CCSPlayerController *pCaller = CallerOf(context);
        if (!pCaller)
            return;

        CBaseEntity *pTarget = FIND_PICKER_ENTITY(pCaller);
        if (!pTarget)
        {
            Reply(context, "Nothing there.");
            return;
        }

        Reply(context, "#%d %s, health %d", pTarget->GetIndex(), pTarget->GetClassname(), pTarget->m_iHealth());
    });

    // The game rules are an entity like any other, and Source2Mod keeps it at hand.
    g_pSource2ModCommands->RegisterConCommand("sample_endround", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        if (CCSGameRules *pGameRules = GET_GAME_RULES())
            pGameRules->TerminateRound(5.0f, 10 /* round draw */);
    });
}

/* ============================================================================
 *
 *   10. Timers
 *
 *   Next frame, once after a delay, or repeating. All of them run on the
 *   main thread, and all of them die with the plugin.
 *
 * ========================================================================== */

void SamplePlugin::SetupTimers()
{
    // NEXT_FRAME is in OnEntitySpawned() (section 5), one-shot timers are in
    // sample_sound_world (section 7). This is the one that repeats.
    g_pSource2ModCommands->RegisterConCommand("sample_countdown", [this](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        // A Timer* is good until the timer ends. A one-shot ends after it
        // fires, a repeating one when it is killed -- by you, or by a map
        // change if it carries TIMER_FLAG_NO_MAPCHANGE, which is why
        // OnLevelShutdown() forgets this pointer.
        if (m_pCountdownTimer)
        {
            KILL_TIMER(m_pCountdownTimer);
            m_pCountdownTimer = nullptr;
        }

        m_nCountdown = args.ArgC() >= 2 ? atoi(args.Arg(1)) : 5;

        m_pCountdownTimer = ADD_TIMER(1.0f, [this]()
        {
            char szMessage[64];
            if (m_nCountdown > 0)
                snprintf(szMessage, sizeof(szMessage), "%d...", m_nCountdown);
            else
                snprintf(szMessage, sizeof(szMessage), "Go!");

            ForEachPlayer([&](CCSPlayerController *pController) { pController->PrintToCenter(szMessage); });

            // A timer may kill itself from inside its own callback.
            if (m_nCountdown-- <= 0)
            {
                KILL_TIMER(m_pCountdownTimer);
                m_pCountdownTimer = nullptr;
            }
        }, TIMER_FLAG_REPEAT | TIMER_FLAG_NO_MAPCHANGE);
    });
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

void SamplePlugin::SetupTransmit()
{
    // sample_hide <slot>: toggles whether the caller sees that player. The
    // controller is what gets hooked -- its visibility is applied to the pawn
    // it drives, the controller itself keeps going so the scoreboard is whole.
    g_pSource2ModCommands->RegisterConCommand("sample_hide", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        CCSPlayerController *pCaller = CallerOf(context);
        if (!pCaller)
        {
            Reply(context, "Only from the game.");
            return;
        }

        if (args.ArgC() < 2)
        {
            Reply(context, "Usage: sample_hide <slot>");
            return;
        }

        const int iSlot = atoi(args.Arg(1));
        CCSPlayerController *pTarget = (iSlot >= 0 && iSlot < 64) ? CCSPlayerController::FromSlot(iSlot) : nullptr;
        if (!pTarget)
        {
            Reply(context, "Nobody in slot %d.", iSlot);
            return;
        }

        // Hooking twice is refused, so ask first. The hook dies with the
        // entity and with the plugin; nothing to undo on Unload().
        if (!g_pSource2ModTransmit->IsEntityHooked(pTarget))
            TRANSMIT_HOOK_ENTITY(pTarget);

        const CPlayerSlot viewer = pCaller->GetPlayerSlot();
        const bool bHidden = !g_pSource2ModTransmit->IsVisible(pTarget, viewer);

        // Channel 0 is enough for one reason. A second feature hiding the
        // same player would use its own channel, so that neither un-hides the
        // other's work: the player shows only when no channel hides them.
        TRANSMIT_SET_VISIBLE(pTarget, viewer, bHidden, 0);
        Reply(context, "%s is now %s for you.", pTarget->GetPlayerName(), bHidden ? "visible" : "hidden");
    });

    // The raw way, for what the model above does not cover: every viewer's
    // transmit set, once per tick. This one keeps the caller's own weapons
    // from everybody else -- the kind of rule that depends on both ends.
    //
    // It runs every tick for every viewer, so the expensive part (who owns
    // what) is done once at the top and only the bit work is per viewer.
    g_pSource2ModTransmit->HookCheckTransmit([](ISource2ModTransmitInfo *const *infos, int infoCount, const uint16_t *entityIndices, int entityCount)
    {
        struct Owned { int index; int ownerSlot; };
        static std::vector<Owned> s_owned;
        s_owned.clear();

        for (int e = 0; e < entityCount; e++)
        {
            CBaseEntity *pEntity = CBaseEntity::FromIndex<CBaseEntity>(entityIndices[e]);
            if (!pEntity || V_strncmp(pEntity->GetClassname(), "weapon_", 7) != 0)
                continue;

            CCSPlayerPawn *pOwner = static_cast<CCSPlayerPawn *>(pEntity->m_hOwnerEntity().Get());
            CCSPlayerController *pController = pOwner ? static_cast<CCSPlayerController *>(pOwner->m_hController().Get()) : nullptr;
            if (!pController)
                continue;

            s_owned.push_back({ entityIndices[e], pController->GetPlayerSlot().Get() });
        }

        for (int i = 0; i < infoCount; i++)
        {
            ISource2ModTransmitInfo *pInfo = infos[i];
            const int viewerSlot = pInfo->GetPlayerSlot().Get();

            for (const Owned &o : s_owned)
            {
                // BlockTransmit() also tells the client to keep a record of the
                // entity, so it comes back later without a "missing client
                // entity" crash. Never just clear the transmit bit.
                if (o.ownerSlot != viewerSlot)
                    pInfo->BlockTransmit(o.index);
            }
        }
    });
}

/* ============================================================================
 *
 *   12. Script natives
 *
 *   What a SourceMod extension does for its scripts: natives a .sp declares
 *   and calls, a library it can test for, and a handle type of our own. A
 *   native gets the calling script's context and its parameters exactly as
 *   SourceMod's do; strings and references go through the context.
 *
 *   The script side:
 *
 *       native int Sample_Add(int a, int b);
 *       native int Sample_Echo(char[] buffer, int maxlen, const char[] fmt, any ...);
 *       native Handle Sample_CreateCounter(int start);
 *       native int Sample_Increment(Handle counter);
 *
 *       if (LibraryExists("cpp_sample")) ...
 *
 *   All of it goes when this plugin unloads: the natives turn into errors,
 *   the library is removed, and every counter handle is closed.
 *
 * ========================================================================== */

static ISource2ModNatives *g_pNatives = nullptr;
static Source2ModHandleType g_CounterType = 0;

// Sample_Add(int a, int b): params[0] is the count, the arguments follow.
static cell_t Native_Add(SourcePawn::IPluginContext *ctx, const cell_t *params)
{
    return params[1] + params[2];
}

// Sample_Echo(char[] buffer, int maxlen, const char[] fmt, any ...): the
// core's formatter, then into the script's buffer.
static cell_t Native_Echo(SourcePawn::IPluginContext *ctx, const cell_t *params)
{
    char szBuffer[256];
    size_t written = 0;
    if (!g_pNatives->FormatString(ctx, params, 3, szBuffer, sizeof(szBuffer), &written))
        return 0;   // the error is reported to the script already

    ctx->StringToLocalUTF8(params[1], params[2], szBuffer, &written);
    return static_cast<cell_t>(written);
}

// Sample_CreateCounter(int start): a handle the script owns -- closed with
// delete / CloseHandle, or when the script unloads.
static cell_t Native_CreateCounter(SourcePawn::IPluginContext *ctx, const cell_t *params)
{
    return g_pNatives->CreateHandle(g_CounterType, new int(params[1]), ctx);
}

static cell_t Native_Increment(SourcePawn::IPluginContext *ctx, const cell_t *params)
{
    int *pCounter = static_cast<int *>(g_pNatives->ReadHandle(ctx, params[1], g_CounterType));
    return pCounter ? ++*pCounter : 0;
}

static const sp_nativeinfo_t g_SampleNatives[] = {
    { "Sample_Add", Native_Add },
    { "Sample_Echo", Native_Echo },
    { "Sample_CreateCounter", Native_CreateCounter },
    { "Sample_Increment", Native_Increment },
    { nullptr, nullptr },
};

void SamplePlugin::SetupScriptNatives()
{
    g_pNatives = (ISource2ModNatives *)g_Source2ModAPI->Source2ModFactory(SOURCE2MOD_NATIVES_INTERFACE, nullptr, nullptr);
    if (!g_pNatives)
    {
        SOURCE2MOD_LOG(this, "No " SOURCE2MOD_NATIVES_INTERFACE " in this core, no script natives.\n");
        return;
    }

    g_CounterType = g_pNatives->CreateHandleType(g_PluginID, "SampleCounter", [](void *object)
    {
        delete static_cast<int *>(object);
    });

    g_pNatives->AddNatives(g_PluginID, g_SampleNatives);
    g_pNatives->RegisterLibrary(g_PluginID, "cpp_sample");
}

/* ============================================================================
 *
 *   Shared helper for the asynchronous sections (16, 17)
 *
 * ========================================================================== */

// A reply that arrives frames later: the command context is gone by then, so
// the slot travels with the request -- the player's chat, or the console for
// -1. The player may have left in between; then nobody is told.
static void ReplyToSlot(int slot, const char *pszFormat, ...)
{
    char szBuffer[512];

    va_list args;
    va_start(args, pszFormat);
    vsnprintf(szBuffer, sizeof(szBuffer), pszFormat, args);
    va_end(args);

    if (slot < 0)
        SOURCE2MOD_CONPRINTF("%s\n", szBuffer);
    else if (CCSPlayerController *pPlayer = CCSPlayerController::FromSlot(slot))
        pPlayer->PrintToChat(szBuffer);
}

static int SlotOf(const Source2ModCommandContext &context)
{
    const CPlayerSlot slot = context.GetPlayerSlot();
    return slot.IsValid() ? slot.Get() : -1;
}

/* ============================================================================
 *
 *   13. Paths and permissions
 *
 *   Where Source2Mod keeps its files, and who may do what: dotted
 *   permissions ("sample.admin"), groups that hold them, immunity, and
 *   commands that check them for you. What this plugin creates or grants is
 *   tagged with it.
 *
 * ========================================================================== */

void SamplePlugin::SetupPathsAndPermissions()
{
    // Absolute paths, no trailing separator. PLUGIN_CONFIG_DIR() is this
    // plugin's own folder under configs/, made on first use.
    SOURCE2MOD_LOG(this, "Root: %s\n", g_pSource2ModPaths->RootDirectory());
    SOURCE2MOD_LOG(this, "Configs: %s\n", g_pSource2ModPaths->ConfigsDirectory());
    SOURCE2MOD_LOG(this, "My configs: %s\n", PLUGIN_CONFIG_DIR());

    // A group of our own that holds a permission of our own. permissions.json
    // can grant the same group to admins; sample_vip below adds you at runtime.
    g_pSource2ModPermissions->CreateGroup(g_PluginID, "sample_vip", 10);
    g_pSource2ModPermissions->AddGroupPermission(g_PluginID, "sample_vip", "sample.admin");

    // A command with a permission: the core checks it before the handler
    // runs, and permissions.json's "Overrides" can re-assign it.
    g_pSource2ModCommands->RegisterConCommand("sample_admin", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        Reply(context, "You have sample.admin.");
    }, "sample.admin");

    // What a player who lacks it is told, instead of the core's default line.
    g_pSource2ModPermissions->SetAccessDeniedHandler([](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, const char *permission)
    {
        Reply(context, "%s needs %s -- try sample_vip.", args.Arg(0), permission);
    });

    // What the caller holds. SteamIDs are 64-bit; 0 until the player is
    // authorized with Steam.
    g_pSource2ModCommands->RegisterConCommand("sample_perm", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        const CPlayerSlot slot = context.GetPlayerSlot();
        if (!slot.IsValid())
        {
            Reply(context, "The console holds everything.");
            return;
        }

        const uint64 steamId = g_pSource2ModPermissions->GetPlayerSteamID(slot);
        const char *pszPermission = args.ArgC() >= 2 ? args.Arg(1) : "sample.admin";

        Reply(context, "%llu: authorized %d, %s %d, sample_vip %d, immunity %d",
              (unsigned long long)steamId, g_pSource2ModPermissions->IsPlayerAuthorized(slot), pszPermission,
              PERMISSIONS_PLAYER_HAS(slot, pszPermission),
              g_pSource2ModPermissions->InGroup(steamId, "sample_vip"),
              g_pSource2ModPermissions->GetImmunity(steamId));
    });

    // Joining and leaving the group. Runtime grants are not written to
    // permissions.json; "s2m admins reload" keeps them.
    g_pSource2ModCommands->RegisterConCommand("sample_vip", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        const CPlayerSlot slot = context.GetPlayerSlot();
        const uint64 steamId = slot.IsValid() ? g_pSource2ModPermissions->GetPlayerSteamID(slot) : 0;
        if (!steamId)
        {
            Reply(context, "For authorized players only.");
            return;
        }

        if (g_pSource2ModPermissions->InGroup(steamId, "sample_vip"))
            PERMISSIONS_REMOVE_FROM_GROUP(steamId, "sample_vip");
        else
            PERMISSIONS_ADD_TO_GROUP(steamId, "sample_vip");

        Reply(context, "sample_vip: %s", g_pSource2ModPermissions->InGroup(steamId, "sample_vip") ? "in" : "out");
    });
}

/* ============================================================================
 *
 *   14. Traces
 *
 *   A ray (or a box) through the world: what it hit, where, and what the
 *   world is made of at a point.
 *
 * ========================================================================== */

void SamplePlugin::SetupTraces()
{
    g_pSource2ModCommands->RegisterConCommand("sample_trace", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        CCSPlayerPawn *pPawn = AlivePawnOf(context);
        if (!pPawn)
            return;

        // From the eyes, along the view, until something stops it. MASK_SHOT
        // is the default -- what a bullet hits; the pawn itself is skipped.
        TraceOptions options;
        const Vector vecStart = pPawn->GetEyePosition();
        TraceResult tr = TRACE_SHAPE(vecStart, pPawn->GetEyeAngles(), pPawn, &options);

        if (!tr.DidHit())
        {
            Reply(context, "Nothing in the way.");
            return;
        }

        CEntityInstance *pHit = tr.HitEntity();
        const Vector vecEnd = tr.EndPos();
        const float flDistance = (vecEnd - vecStart).Length();

        // The contents at the end point, filtered by a mask: is it water?
        const uint64 nWater = POINT_CONTENTS(&vecEnd, CONTENTS_WATER | CONTENTS_SLIME);

        Reply(context, "Hit %s at %.0f units (%.0f %.0f %.0f)%s",
              pHit ? pHit->GetClassname() : "the world", flDistance,
              vecEnd.x, vecEnd.y, vecEnd.z, nWater ? ", in water" : "");
    });
}

/* ============================================================================
 *
 *   15. Game systems and addresses
 *
 *   Game systems are engine singletons, reached by their factory name. And
 *   the game functions the core resolved from its own gamedata (ADDR_*): no
 *   signature of your own, a core update after a game update.
 *
 * ========================================================================== */

void SamplePlugin::SetupGameSystems()
{
    if (g_pSource2ModGameSystems->IsAvailable())
    {
        IGameSystem *pSpawnGroups = g_pSource2ModGameSystems->FindByName("SpawnGroupManagerGameSystem");
        SOURCE2MOD_LOG(this, "SpawnGroupManagerGameSystem: %s\n", pSpawnGroups ? "found" : "missing");
    }

    // sample_remove: removes what is under the crosshair through UTIL_Remove,
    // called by the address the core resolved -- section 8 calls by gamedata
    // name, this is the third way.
    g_pSource2ModCommands->RegisterConCommand("sample_remove", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        CCSPlayerController *pCaller = CallerOf(context);
        if (!pCaller)
            return;

        CBaseEntity *pTarget = FIND_PICKER_ENTITY(pCaller);
        if (!pTarget || !V_strcmp(pTarget->GetClassname(), "player"))
        {
            Reply(context, "Aim at something that is not a player.");
            return;
        }

        // Null when the core could not resolve it on this game version.
        if (auto pfnRemove = ADDR_UTIL_REMOVE())
        {
            Reply(context, "Removing #%d %s.", pTarget->GetIndex(), pTarget->GetClassname());
            pfnRemove(pTarget);
        }
    });
}

/* ============================================================================
 *
 *   16. JSON and HTTP
 *
 *   Parsing a document and walking its values; a request with headers whose
 *   answer comes back on a later frame, already parsed when it is JSON.
 *
 * ========================================================================== */

void SamplePlugin::SetupJsonAndHttp()
{
    // A document owns its values: read them while it lives, Release() it once.
    ISource2ModJSONDocument *pDoc = g_pSource2ModJSON->Parse(R"({ "name": "cpp_sample", "sections": [1, 2, 3], "nested": { "ok": true } })");
    if (pDoc->IsValid())
    {
        const ISource2ModJSONValue *pRoot = pDoc->Root();
        const ISource2ModJSONValue *pSections = pRoot->Get("sections");
        const ISource2ModJSONValue *pNested = pRoot->Get("nested");

        int nSum = 0;
        for (int i = 0; pSections && i < pSections->Count(); i++)
            nSum += pSections->At(i)->AsInt();

        SOURCE2MOD_LOG(this, "JSON: %s, %d sections summing to %d, nested.ok %d\n",
                    pRoot->GetString("name"), pSections ? pSections->Count() : 0, nSum,
                    pNested ? pNested->GetBool("ok") : false);
    }
    else
    {
        SOURCE2MOD_LOG(this, "JSON error: %s\n", pDoc->GetError());
    }
    pDoc->Release();

    // sample_http [url]: a GET through Steam's HTTP client, off the game
    // thread; the callback runs on a game frame. GitHub wants a User-Agent.
    g_pSource2ModCommands->RegisterConCommand("sample_http", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        if (!g_pSource2ModHTTP->IsAvailable())
        {
            Reply(context, "Steam's HTTP client is not up yet.");
            return;
        }

        const char *pszUrl = args.ArgC() >= 2 ? args.Arg(1) : "https://api.github.com/repos/alliedmodders/sourcepawn";
        const std::vector<Source2ModHTTPHeader> headers = { { "User-Agent", "Source2Mod-cpp_sample" } };
        const int slot = SlotOf(context);

        HTTP_GET(pszUrl, [slot](const Source2ModHTTPResponse &response)
        {
            if (!response.m_bSuccess)
            {
                ReplyToSlot(slot, "The request failed.");
                return;
            }

            // m_pJson is the body parsed, valid during this callback only.
            if (const ISource2ModJSONValue *pJson = response.m_pJson)
                ReplyToSlot(slot, "HTTP %d: %s, %d stars", response.m_nStatusCode, pJson->GetString("full_name", "?"), pJson->GetInt("stargazers_count"));
            else
                ReplyToSlot(slot, "HTTP %d: %d bytes, not JSON", response.m_nStatusCode, (int)response.m_sBody.size());
        }, &headers);
    });
}

/* ============================================================================
 *
 *   17. MySQL
 *
 *   Queries run off the game thread; their callbacks come back on a game
 *   frame. Serverless connects per query; a connection stays open and runs
 *   transactions. Set sample_mysql_* to a server to try it.
 *
 * ========================================================================== */

CConVar<CUtlString> sample_mysql_host("sample_mysql_host", FCVAR_PROTECTED, "MySQL host for 17. MySQL (empty = off)", "");
CConVar<CUtlString> sample_mysql_user("sample_mysql_user", FCVAR_PROTECTED, "MySQL user", "root");
CConVar<CUtlString> sample_mysql_pass("sample_mysql_pass", FCVAR_PROTECTED, "MySQL password", "");
CConVar<CUtlString> sample_mysql_db("sample_mysql_db", FCVAR_PROTECTED, "MySQL database", "test");

static Source2ModMySQLConnectionInfo MySQLInfo()
{
    Source2ModMySQLConnectionInfo info;
    info.host = sample_mysql_host.Get().Get();
    info.user = sample_mysql_user.Get().Get();
    info.pass = sample_mysql_pass.Get().Get();
    info.database = sample_mysql_db.Get().Get();
    return info;
}

void SamplePlugin::SetupMySQL()
{
    // sample_mysql [text]: one query with a "?" parameter, escaped for you.
    g_pSource2ModCommands->RegisterConCommand("sample_mysql", [this](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        if (sample_mysql_host.Get().IsEmpty())
        {
            Reply(context, "Set sample_mysql_host (and _user, _pass, _db) first.");
            return;
        }

        if (!m_pServerless)
            m_pServerless = g_pSource2ModMySQL->CreateServerless(g_PluginID, MySQLInfo());

        const int slot = SlotOf(context);
        m_pServerless->Query("SELECT ? AS greeting, NOW() AS now", { args.ArgC() >= 2 ? args.Arg(1) : "hello" },
            [slot](const Source2ModMySQLServerlessResult &result)
        {
            if (!result.m_bSuccess)
            {
                ReplyToSlot(slot, "MySQL error %u: %s", result.m_nErrorCode, result.m_sError.c_str());
                return;
            }

            ISource2ModMySQLResult *pRows = result.m_pQuery ? result.m_pQuery->GetResultSet() : nullptr;
            if (pRows && pRows->FetchRow())
                ReplyToSlot(slot, "MySQL says %s at %s", pRows->GetString(0), pRows->GetString(1));
        });
    });

    // sample_mysql_txn: a connection, then two queries as one transaction.
    g_pSource2ModCommands->RegisterConCommand("sample_mysql_txn", [this](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        if (sample_mysql_host.Get().IsEmpty())
        {
            Reply(context, "Set sample_mysql_host (and _user, _pass, _db) first.");
            return;
        }

        const int slot = SlotOf(context);
        if (!m_pConnection)
            m_pConnection = MYSQL_CREATE_CONNECTION(MySQLInfo());

        m_pConnection->Connect([this, slot](bool bConnected)
        {
            if (!bConnected)
            {
                ReplyToSlot(slot, "Could not connect.");
                return;
            }

            Source2ModMySQLTransaction txn;
            txn.queries = { "SELECT VERSION()", "SELECT COUNT(*) FROM information_schema.tables" };

            m_pConnection->ExecuteTransaction(txn, [slot](std::vector<ISource2ModMySQLQuery *> queries)
            {
                // One result per query, in order.
                ISource2ModMySQLResult *pVersion = queries[0]->GetResultSet();
                ISource2ModMySQLResult *pTables = queries[1]->GetResultSet();
                if (pVersion && pVersion->FetchRow() && pTables && pTables->FetchRow())
                    ReplyToSlot(slot, "MySQL %s, %d tables", pVersion->GetString(0), pTables->GetInt(0));
            }, [slot](std::string error, int failIndex)
            {
                ReplyToSlot(slot, "Transaction failed at query %d: %s", failIndex, error.c_str());
            });
        });
    });
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

// The menu lives as long as the library: the core closes it for whoever has
// it open when the plugin unloads, and that happens after Unload().
static CenterHtmlMenu s_SampleMenu("cpp_sample menu");

void SamplePlugin::SetupMenus()
{
    s_SampleMenu.AddMenuOption("Heal to 100", [](CCSPlayerController *pPlayer, ChatMenuOption &option)
    {
        CCSPlayerPawn *pPawn = pPlayer->GetPlayerPawn();
        if (pPawn && pPlayer->m_bPawnIsAlive())
            pPawn->m_iHealth = 100;
    });

    s_SampleMenu.AddMenuOption("Give an AK-47", [](CCSPlayerController *pPlayer, ChatMenuOption &option)
    {
        CCSPlayerPawn *pPawn = pPlayer->GetPlayerPawn();
        if (pPawn && pPlayer->m_bPawnIsAlive() && pPawn->m_pItemServices())
            pPawn->m_pItemServices()->GiveNamedItem("weapon_ak47");
    });

    // Shown, but cannot be picked.
    s_SampleMenu.AddMenuOption("Not today", [](CCSPlayerController *, ChatMenuOption &) {}, true);

    // What happens after a pick: close, go back to the first page, or stay.
    s_SampleMenu.SetPostSelectAction(PostSelectAction::Close);

    g_pSource2ModCommands->RegisterConCommand("sample_menu", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        if (CCSPlayerController *pCaller = CallerOf(context))
            OPEN_CENTER_HTML_MENU(pCaller, &s_SampleMenu);
        else
            Reply(context, "Menus are for players.");
    });

    // sample_hud: hooks the button clicks of every custom HUD layout on the
    // map. The hook goes with the layout entity, or with the plugin.
    g_pSource2ModCommands->RegisterConCommand("sample_hud", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        int nHooked = 0;

        CBaseEntity *pEntity = nullptr;
        while ((pEntity = FIND_ENTITY_BY_CLASSNAME(pEntity, "custom_hud_layout")) != nullptr)
        {
            HOOK_CUSTOM_HUD_CLICK(static_cast<CCSCustomHudLayout *>(pEntity), [](CCSPlayerController *pPlayer, CCSCustomHudLayout *pLayout, const char *pszButtonId)
            {
                SOURCE2MOD_LOG(&g_Plugin, "%s clicked \"%s\" on a custom HUD layout\n", pPlayer ? pPlayer->GetPlayerName() : "?", pszButtonId);
            });
            nHooked++;
        }

        Reply(context, "Hooked %d custom HUD layout(s).", nHooked);
    });
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

static const char *s_pszSampleScript =
    "import { Instance } from \"cs_script/point_script\";\n"
    "Instance.ServerCommand(\"source2mod_script cpp_sample \" + encodeURIComponent(\"hello from cs_script\"));\n"
    "Instance.OnScriptInput(\"ping\", () => Instance.ServerCommand(\"source2mod_script cpp_sample pong\"));\n";

void SamplePlugin::SetupScripts()
{
    // Everything a script sends on "cpp_sample", a frame or more later.
    HOOK_SCRIPT_MESSAGE("cpp_sample", [](const char *pszChannel, const char *pszPayload)
    {
        SOURCE2MOD_LOG(&g_Plugin, "Script message on %s: %s\n", pszChannel, pszPayload);
    });

    // sample_script: runs the source above as a point_script named
    // cpp_sample_script; running it again replaces it (a hot reload).
    g_pSource2ModCommands->RegisterConCommand("sample_script", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        if (!g_pSource2ModScripts->CanRunSource())
        {
            Reply(context, "This server cannot compile cs_script source.");
            return;
        }

        CEntityHandle hScript = SCRIPTS_RUN_SOURCE("cpp_sample_script", "scripts/cpp_sample_script.vjs", s_pszSampleScript, false);
        Reply(context, "cpp_sample_script: %s", hScript.IsValid() ? "running" : "failed");
    });

    // sample_script_ping: native -> script; the script answers "pong".
    g_pSource2ModCommands->RegisterConCommand("sample_script_ping", [](const Source2ModCommandContext& context, const Source2ModCommandArgs& args, bool post)
    {
        if (!SCRIPTS_FIRE_INPUT("cpp_sample_script", "ping"))
            Reply(context, "Run sample_script first.");
    });
}
