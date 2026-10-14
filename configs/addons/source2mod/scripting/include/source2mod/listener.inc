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
