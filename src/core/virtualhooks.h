/**
* vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * Copyright (C) 2025-2026 Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl.
 * All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 * gives you permission to link the code of this program
 * (as well as its derivative works) to "Counter-Strike 2," "Source 2,"
 * "Steam," and any Game MODs or server software running on software by
 * Valve Corporation. You must obey the GNU General Public License in all
 * respects for all other code used.
 *
 * Additionally, this exception applies to all derivative works unless
 * otherwise stated in LICENSE.txt.
 *
 * Authors:
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */
#pragma once
// tier1/convar.h first: iloopmode.h (pulled in by igamesystem.h) uses
// CSplitScreenSlot in IGameSystem::HandleInputEvent without declaring it, and
// PostEventAbstract below needs the type too.
#include "tier0/convar.h"

// KHook, via metamod.
#include "ISmmPlugin.h"
#include "source2mod/ISource2ModKHook.h"

#include "igameevents.h"
#include "igamesystem.h"
#include "engine2/isource2server.h"
#include "engine2/ivengineserver2.h"
#include "engine2/inetworkserverservice.h"
#include "engine2/gamesessionconfiguration.h"
#include "entitysystem.h"
#include "gameentitysystem.h"
#include "engine2/igameeventsystem.h"
#include "engine2/serversideclientbase.h"

#include "dynlibutils/memaddr.hpp"

class IEntityInstance; // forward-declare global SDK interface (defined in IEntityInstance.h)

namespace virtualhooks {
    class Virtuals {
    public:
        Virtuals();

        void InitListeners();
        void DestructListeners();
    public:
        KHook::Return<void> Hook_GameFrame(ISource2Server* pThis, bool simulating, bool bFirstTick, bool bLastTick);
        KHook::Return<void> Hook_StartupServer(INetworkServerService* pThis, const GameSessionConfiguration_t& config, ISource2WorldSession* pWorldSession, const char* pszMapName);
        KHook::Return<void> Hook_DispatchConCommand(ICvar* pThis, ConCommandRef cmd, const CCommandContext& ctx, const CCommand& args);
        KHook::Return<void> Hook_ClientCommand(ISource2GameClients* pThis, CPlayerSlot slot, const CCommand& args);
        KHook::Return<void> Hook_ClientPutInServer(ISource2GameClients* pThis, CPlayerSlot slot, const char* pszName, int type, uint64 xuid);
        KHook::Return<void> Hook_ClientVoice(ISource2GameClients* pThis, CPlayerSlot slot);
        KHook::Return<void> Hook_ClientSettingsChanged(ISource2GameClients* pThis, CPlayerSlot slot);
        KHook::Return<void> Hook_ClientDisconnect(ISource2GameClients* pThis, CPlayerSlot slot, ENetworkDisconnectionReason reason, const char* pszName, uint64 xuid, const char* pszNetworkID);
        KHook::Return<void> Hook_ClientDisconnectPost(ISource2GameClients* pThis, CPlayerSlot slot, ENetworkDisconnectionReason reason, const char* pszName, uint64 xuid, const char* pszNetworkID);
        KHook::Return<bool> Hook_ClientConnect(ISource2GameClients* pThis, CPlayerSlot slot, const char* pszName, uint64 xuid, const char* pszNetworkID, bool unk1, CBufferString* pRejectReason);
        KHook::Return<void> Hook_OnClientConnected(ISource2GameClients* pThis, CPlayerSlot slot, const char* pszName, uint64 xuid, const char* pszNetworkID, const char* pszAddress, bool bFakePlayer);
        KHook::Return<void> Hook_ClientSvcUserMessage(ISource2GameClients* pThis, CPlayerSlot slot, int nType, uint32 nSize, const void* pBuffer);
        KHook::Return<void> Hook_GameServerSteamAPIActivated(ISource2Server* pThis);
        KHook::Return<void> Hook_GameServerSteamAPIDeactivated(ISource2Server* pThis);
        KHook::Return<void> Hook_PostEventAbstract(IGameEventSystem* pThis, CSplitScreenSlot nSlot, bool bLocalOnly, int nClientCount, const uint64* clients, INetworkMessageInternal* pEvent, const CNetMessage* pData, unsigned long nSize, NetChannelBufType_t bufType);
        KHook::Return<void> Hook_OnServerGamePostSimulate(IGameSystem* pThis, const EventServerGamePostSimulate_t* const pMsg);
        KHook::Return<int>  Hook_LoadEventsFromFile(IGameEventManager2* pThis, const char* filename, bool bSearchAll);
        KHook::Return<bool> Hook_FireEvent(IGameEventManager2* pThis, IGameEvent* event, bool bDontBroadcast);
        KHook::Return<bool> Hook_FireEventPost(IGameEventManager2* pThis, IGameEvent* event, bool bDontBroadcast);
        KHook::Return<bool> Hook_SendNetMessage(CServerSideClientBase* pThis, const CNetMessage* pData, NetChannelBufType_t bufType);
        KHook::Return<bool> Hook_FilterMessage(INetworkMessageProcessingPreFilter* pThis, const CNetMessage* pData, INetChannel* pChannel);
        KHook::Return<void> Hook_CheckTransmit(ISource2GameEntities* pThis, CCheckTransmitInfo** ppInfoList, int nInfoCount, CBitVec<16384>& unionTransmitEdicts, CBitVec<16384>& unionTransmitEdicts2, const Entity2Networkable_t** pNetworkables, const uint16* pEntityIndicies, int nEntities);
    protected:
        // KHook hooks only come down in their destructor, so they live behind
        // plain pointers: new in the constructor, delete in DestructListeners().
        Source2ModKHook::Virtual<ISource2Server, void, bool, bool, bool>* m_hGameFrame = nullptr;
        Source2ModKHook::Virtual<INetworkServerService, void, const GameSessionConfiguration_t&, ISource2WorldSession*, const char*>* m_hStartupServer = nullptr;
        Source2ModKHook::Virtual<ICvar, void, ConCommandRef, const CCommandContext&, const CCommand&>* m_hDispatchConCommand = nullptr;
        Source2ModKHook::Virtual<ISource2GameClients, void, CPlayerSlot, const CCommand&>* m_hClientCommand = nullptr;
        // Fanned out to the plugins' ISource2ModListener; Source2Mod itself has
        // no use for them.
        Source2ModKHook::Virtual<ISource2GameClients, void, CPlayerSlot, const char*, int, uint64>* m_hClientPutInServer = nullptr;
        Source2ModKHook::Virtual<ISource2GameClients, bool, CPlayerSlot, const char*, uint64, const char*, bool, CBufferString*>* m_hClientConnect = nullptr;
        Source2ModKHook::Virtual<ISource2GameClients, void, CPlayerSlot, const char*, uint64, const char*, const char*, bool>* m_hOnClientConnected = nullptr;
        Source2ModKHook::Virtual<ISource2GameClients, void, CPlayerSlot>* m_hClientVoice = nullptr;
        Source2ModKHook::Virtual<ISource2GameClients, void, CPlayerSlot>* m_hClientSettingsChanged = nullptr;
        // "meta unload <this plugin>" typed at the console: superseded, and
        // finished from the next GameFrame, see Hook_DispatchConCommand.
        bool m_bSelfUnloadPending = false;
        Source2ModKHook::Virtual<ISource2GameClients, void, CPlayerSlot, int, uint32, const void*>* m_hClientSvcUserMessage = nullptr;
        Source2ModKHook::Virtual<ISource2GameClients, void, CPlayerSlot, ENetworkDisconnectionReason, const char*, uint64, const char*>* m_hClientDisconnect = nullptr;
        Source2ModKHook::Virtual<ISource2Server, void>* m_hSteamAPIActivated = nullptr;
        Source2ModKHook::Virtual<ISource2Server, void>* m_hSteamAPIDeactivated = nullptr;
        Source2ModKHook::Virtual<IGameEventSystem, void, CSplitScreenSlot, bool, int, const uint64*, INetworkMessageInternal*, const CNetMessage*, unsigned long, NetChannelBufType_t>* m_hPostEventAbstract = nullptr;
        Source2ModKHook::Virtual<IGameSystem, void, const EventServerGamePostSimulate_t*>* m_hOnServerGamePostSimulate = nullptr;
        Source2ModKHook::Virtual<IGameEventManager2, int, const char*, bool>* m_hLoadEventsFromFile = nullptr;
        // Pre and Post on the one object.
        Source2ModKHook::Virtual<IGameEventManager2, bool, IGameEvent*, bool>* m_hFireEvent = nullptr;
        // SendNetMessage is declared on the base, so that is what the member
        // function pointer -- and therefore the hook -- is typed against.
        Source2ModKHook::Virtual<CServerSideClientBase, bool, const CNetMessage*, NetChannelBufType_t>* m_hSendNetMessage = nullptr;
        // Incoming client messages: the only virtual of CServerSideClient's
        // pre-filter base, hooked on that base's own vtable.
        Source2ModKHook::Virtual<INetworkMessageProcessingPreFilter, bool, const CNetMessage*, INetChannel*>* m_hFilterMessage = nullptr;
        // Post only: the engine decides first, the transmit manager then takes
        // hidden entities back out (core/transmit.h).
        Source2ModKHook::Virtual<ISource2GameEntities, void, CCheckTransmitInfo**, int, CBitVec<16384>&, CBitVec<16384>&, const Entity2Networkable_t**, const uint16*, int>* m_hCheckTransmit = nullptr;

        // Vtables of engine classes with no interface to fetch, resolved by
        // RTTI name. Each also doubles as the stand-in object AddGlobal()
        // reads the vtable off (it only ever looks at the first pointer), which
        // then covers every instance sharing it.
        void* m_pCEntityDebugGameSystemVTable = nullptr;
        void* m_pCGameEventManagerVTable = nullptr;
        void* m_pCServerSideClientVTable = nullptr;
        // CServerSideClient's INetworkMessageProcessingPreFilter vtable, found by
        // the base's offset in the RTTI (+48 Linux, +8 Windows).
        void* m_pCServerSideClientPreFilterVTable = nullptr;
    };

    class CEntityListener: public IEntityListener {
    public:
        void OnEntityCreated(CEntityInstance* pEntity) override;
        void OnEntitySpawned(CEntityInstance* pEntity) override;
        void OnEntityDeleted(CEntityInstance* pEntity) override;
        void OnEntityParentChanged(CEntityInstance* pEntity, CEntityInstance* pNewParent) override;
    };

    extern Virtuals virtuals;
    extern CEntityListener entityListener;
}
