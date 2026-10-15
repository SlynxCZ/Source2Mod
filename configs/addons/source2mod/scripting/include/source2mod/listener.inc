/**
 * Source2Mod SourcePawn API: ISource2ModListener as forwards.
 *
 * Each is called in every script that has it, as the C++ listener is called
 * in every plugin that registered one. Slots are 0-based; 64-bit SteamIDs
 * (xuid) are decimal text.
 */

#if defined _source2mod_listener_included
 #endinput
#endif
#define _source2mod_listener_included

#include <source2mod/types>

forward void Source2Mod_OnLevelInit(const char[] mapName, const char[] mapEntities, const char[] oldLevel, const char[] landmarkName, bool loadGame, bool background);

forward void Source2Mod_OnLevelShutdown();

/** Post ISource2Server::GameFrame: every tick, after Source2Mod's own per-frame work -- keep it cheap. */
forward void Source2Mod_OnGameFrame(bool simulating, bool firstTick, bool lastTick);

/**
 * Post INetworkServerService::StartupServer: a new server session. The
 * session configuration and world session are engine objects without a
 * script view and are not passed.
 */
forward void Source2Mod_OnStartupServer(const char[] mapName);

/**
 * Pre ISource2GameClients::ClientConnect: a player asks to join (the engine
 * does not call it for bots), right after OnClientConnected. networkId is
 * the player's "ip:port" here. Return false to refuse; the reason goes into
 * rejectReason (the CS2 client shows its own "rejected by game" text
 * whatever it says). The first plugin to refuse ends it -- the ones after it
 * are not asked.
 */
forward bool Source2Mod_OnClientConnect(CPlayerSlot slot, const char[] name, const char[] xuid, const char[] networkId, bool unk1, char[] rejectReason, int maxlen);

/**
 * Post ISource2GameClients::OnClientConnected: a player or a bot (fakePlayer)
 * got a slot. For a player the engine calls this before ClientConnect, so it
 * also comes for a connection that is then refused. networkId is the SteamID
 * ("[U:1:...]"), address the player's "ip:port".
 */
forward void Source2Mod_OnClientConnected(CPlayerSlot slot, const char[] name, const char[] xuid, const char[] networkId, const char[] address, bool fakePlayer);

/** Post ISource2GameClients::ClientPutInServer: the player (type 0) or bot (type 1) is in the game and has a controller. */
forward void Source2Mod_OnClientPutInServer(CPlayerSlot slot, const char[] name, int type, const char[] xuid);

/** Pre ISource2GameClients::ClientDisconnect: the player is leaving and everything of theirs is still there. */
forward void Source2Mod_OnClientDisconnect(CPlayerSlot slot, int reason, const char[] name, const char[] xuid, const char[] networkId);

/** Post ISource2GameClients::ClientDisconnect: the game is done with the player. */
forward void Source2Mod_OnClientDisconnectPost(CPlayerSlot slot, int reason, const char[] name, const char[] xuid, const char[] networkId);

/** Post ISource2GameClients::ClientVoice: the player in this slot is talking. */
forward void Source2Mod_OnClientVoice(CPlayerSlot slot);

/** Post ISource2GameClients::ClientSettingsChanged: the player's convars (name, language, ...) changed. */
forward void Source2Mod_OnClientSettingsChanged(CPlayerSlot slot);

/** Post ISource2Server::GameServerSteamAPIActivated: the Steam API is up. */
forward void Source2Mod_OnGameServerSteamAPIActivated();

/** Pre ISource2Server::GameServerSteamAPIDeactivated: the Steam API is about to go. */
forward void Source2Mod_OnGameServerSteamAPIDeactivated();

/** Post IGameEventManager2::LoadEventsFromFile: the game loaded an event description file. */
forward void Source2Mod_OnLoadEventsFromFile(const char[] filename, bool searchAll);

/** Steam validated the player in this slot: from now on they are checked under steamId. Not called for bots. */
forward void Source2Mod_OnClientAuthorized(CPlayerSlot slot, const char[] steamId);

/** Steam refused the ticket of the player in this slot; steamId is the one they claimed. */
forward void Source2Mod_OnClientAuthorizeFailed(CPlayerSlot slot, const char[] steamId);

/** What this SteamID64 holds may have changed; "0" when a change can touch anybody. */
forward void Source2Mod_OnPermissionsChanged(const char[] steamId);

/** Another Source2Mod plugin (native or script) loaded / is unloading; `id` is its plugin id. */
forward void Source2Mod_OnPluginLoad(int id, const char[] name);

forward void Source2Mod_OnPluginUnload(int id, const char[] name);

/** A Metamod plugin loaded / unloaded (Metamod's numbering). */
forward void Source2Mod_OnMetamodPluginLoad(int id);

forward void Source2Mod_OnMetamodPluginUnload(int id);

/** Every Source2Mod plugin of the startup batch is loaded (SourceMod's OnAllPluginsLoaded runs right after). */
forward void Source2Mod_OnAllSource2ModPluginsLoaded();

forward void Source2Mod_OnAllMetamodPluginsLoaded();
