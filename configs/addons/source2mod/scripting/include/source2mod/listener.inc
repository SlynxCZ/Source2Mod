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

/** Every tick, right after the game's GameFrame -- keep it cheap. */
forward void Source2Mod_OnGameFrame(bool simulating, bool firstTick, bool lastTick);

forward void Source2Mod_OnClientPutInServer(CPlayerSlot slot, const char[] name, int type, const char[] xuid);

forward void Source2Mod_OnClientDisconnect(CPlayerSlot slot, int reason, const char[] name, const char[] xuid, const char[] networkId);

forward void Source2Mod_OnClientAuthorized(CPlayerSlot slot, const char[] steamId);

forward void Source2Mod_OnClientAuthorizeFailed(CPlayerSlot slot, const char[] steamId);

forward void Source2Mod_OnClientVoice(CPlayerSlot slot);

forward void Source2Mod_OnClientSettingsChanged(CPlayerSlot slot);

forward void Source2Mod_OnPermissionsChanged(const char[] steamId);

forward void Source2Mod_OnGameServerSteamAPIActivated();

forward void Source2Mod_OnGameServerSteamAPIDeactivated();

/** Another Source2Mod plugin (native or script) loaded / is unloading; `id` is its plugin id. */
forward void Source2Mod_OnPluginLoad(int id, const char[] name);

forward void Source2Mod_OnPluginUnload(int id, const char[] name);

/** A Metamod plugin loaded / unloaded (Metamod's numbering). */
forward void Source2Mod_OnMetamodPluginLoad(int id);

forward void Source2Mod_OnMetamodPluginUnload(int id);

/** Every Source2Mod plugin of the startup batch is loaded (SourceMod's OnAllPluginsLoaded runs right after). */
forward void Source2Mod_OnAllSource2ModPluginsLoaded();

forward void Source2Mod_OnAllMetamodPluginsLoaded();

/**
 * The server is starting a map session (IServerGameDLL StartupServer). The
 * session configuration and world session are engine objects without a
 * script view and are not passed.
 */
forward void Source2Mod_OnStartupServer(const char[] mapName);

/** The game event manager loaded an event description file (IGameEventManager2.LoadEventsFromFile). */
forward void Source2Mod_OnLoadEventsFromFile(const char[] filename, bool searchAll);
