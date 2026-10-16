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
#include "virtualhooks.h"

#include "commands.h"
#include "core/entities.h"
#include "core/menus.h"
#include "core/scheduler.h"
#include "crashhandler.h"
#include "customhud.h"
#include "events.h"
#include "gamehooks.h"
#include "http.h"
#include "mysql.h"
#include "networkmessages.h"
#include "cookies.h"
#include "permissions.h"
#include "plugin.h"
#include "pluginmanager.h"
#include "scripts.h"
#include "shared.h"
#include "slotinput.h"
#include "sounds.h"
#include "tkvprof.h"
#include "transmit.h"
#include "utils/log.h"

#include <dynlibutils/module.hpp>
#include <engine2/igameeventsystem.h>
#include <engine2/inetworkserverservice.h>
#include <engine2/networkgameserverbase.h>
#include <source2mod/schema/entity/classes/CBaseEntity.h>
#include <source2mod/schema/entity/classes/CCSCustomHudLayout.h>
#include <source2mod/schema/entity/classes/CCSGameRulesProxy.h>
#include <source2mod/schema/entity/classes/CCSPlayerController.h>
#include <source2mod/schema/entity/enums/ECstrike15UserMessages.h>
#include <source2mod/schema/schema.h>
#include <source2mod/utils/plat.h>
#include <steam/isteamgameserver.h>

namespace virtualhooks
{
    Virtuals virtuals;
    CEntityListener entityListener;

    static std::vector<IGameEvent*> eventStack;

    Virtuals::Virtuals() :
        KHOOK_NEW(m_hGameFrame, &ISource2Server::GameFrame, this, &Virtuals::Hook_GameFramePre, &Virtuals::Hook_GameFrame),
        KHOOK_NEW(m_hStartupServer, &INetworkServerService::StartupServer, this, nullptr, &Virtuals::Hook_StartupServer),
        KHOOK_NEW(m_hDispatchConCommand, &ICvar::DispatchConCommand, this, &Virtuals::Hook_DispatchConCommand, nullptr),
        KHOOK_NEW(m_hClientCommand, &ISource2GameClients::ClientCommand, this, &Virtuals::Hook_ClientCommand, nullptr),
        KHOOK_NEW(m_hClientPutInServer, &ISource2GameClients::ClientPutInServer, this, nullptr, &Virtuals::Hook_ClientPutInServer),
        KHOOK_NEW(m_hClientVoice, &ISource2GameClients::ClientVoice, this, nullptr, &Virtuals::Hook_ClientVoice),
        KHOOK_NEW(m_hClientSettingsChanged, &ISource2GameClients::ClientSettingsChanged, this, nullptr, &Virtuals::Hook_ClientSettingsChanged),
        KHOOK_NEW(m_hClientSvcUserMessage, &ISource2GameClients::ClientSvcUserMessage, this, &Virtuals::Hook_ClientSvcUserMessage, nullptr),
        KHOOK_NEW(m_hClientDisconnect, &ISource2GameClients::ClientDisconnect, this, &Virtuals::Hook_ClientDisconnect, &Virtuals::Hook_ClientDisconnectPost),
        KHOOK_NEW(m_hClientConnect, &ISource2GameClients::ClientConnect, this, &Virtuals::Hook_ClientConnect, nullptr),
        KHOOK_NEW(m_hOnClientConnected, &ISource2GameClients::OnClientConnected, this, nullptr, &Virtuals::Hook_OnClientConnected),
        KHOOK_NEW(m_hSteamAPIActivated, &ISource2Server::GameServerSteamAPIActivated, this, nullptr, &Virtuals::Hook_GameServerSteamAPIActivated),
        KHOOK_NEW(m_hSteamAPIDeactivated, &ISource2Server::GameServerSteamAPIDeactivated, this, &Virtuals::Hook_GameServerSteamAPIDeactivated, nullptr),
        // PostEventAbstract is overloaded, so the member function pointer has to be disambiguated explicitly.
        KHOOK_NEW(m_hPostEventAbstract, static_cast<void (IGameEventSystem::*)(CSplitScreenSlot, bool, int, const uint64*, INetworkMessageInternal*, const CNetMessage*, unsigned long, NetChannelBufType_t)>(&IGameEventSystem::PostEventAbstract), this, &Virtuals::Hook_PostEventAbstract, nullptr),
        KHOOK_NEW(m_hOnServerGamePostSimulate, &IGameSystem::OnServerGamePostSimulate, this, nullptr, &Virtuals::Hook_OnServerGamePostSimulate),
        KHOOK_NEW(m_hLoadEventsFromFile, &IGameEventManager2::LoadEventsFromFile, this, nullptr, &Virtuals::Hook_LoadEventsFromFile),
        KHOOK_NEW(m_hFireEvent, &IGameEventManager2::FireEvent, this, &Virtuals::Hook_FireEvent, &Virtuals::Hook_FireEventPost),
        KHOOK_NEW(m_hSendNetMessage, &CServerSideClientBase::SendNetMessage, this, &Virtuals::Hook_SendNetMessage, nullptr),
        KHOOK_NEW(m_hFilterMessage, &INetworkMessageProcessingPreFilter::FilterMessage, this, &Virtuals::Hook_FilterMessage, nullptr),
        KHOOK_NEW(m_hCheckTransmit, &ISource2GameEntities::CheckTransmit, this, nullptr, &Virtuals::Hook_CheckTransmit)
    {
    }

    // The slot the member function pointer compiled in, checked against
    // gamedata: an entry for this platform wins (it can be fixed without a
    // rebuild), a difference is said out loud, no entry leaves the header's.
    template <typename HOOK>
    static void ResolveIndex(HOOK* hook, const char* pszName)
    {
        const int index = shared::g_pGameConfig ? shared::g_pGameConfig->GetOffset(pszName) : -1;

        if (index < 0)
        {
            FP_INFO("No gamedata offset '{}' for this platform; using the interface's index {}", pszName, hook->GetIndex());
            return;
        }

        if (hook->GetIndex() != index)
            FP_WARN("Gamedata offset '{}' is {} but the interface header compiled in {}; using gamedata", pszName, index, hook->GetIndex());

        hook->Configure(index);
    }

    void Virtuals::InitListeners()
    {
        DynLibUtils::CModule libserver(g_pSource2Server);
        DynLibUtils::CModule libengine(g_pEngineServer);

        ResolveIndex(m_hGameFrame, "ISource2Server::GameFrame");
        ResolveIndex(m_hStartupServer, "INetworkServerService::StartupServer");
        ResolveIndex(m_hDispatchConCommand, "ICvar::DispatchConCommand");
        ResolveIndex(m_hClientCommand, "ISource2GameClients::ClientCommand");
        ResolveIndex(m_hClientPutInServer, "ISource2GameClients::ClientPutInServer");
        ResolveIndex(m_hClientVoice, "ISource2GameClients::ClientVoice");
        ResolveIndex(m_hClientSettingsChanged, "ISource2GameClients::ClientSettingsChanged");
        ResolveIndex(m_hClientSvcUserMessage, "ISource2GameClients::ClientSvcUserMessage");
        ResolveIndex(m_hClientDisconnect, "ISource2GameClients::ClientDisconnect");
        ResolveIndex(m_hClientConnect, "ISource2GameClients::ClientConnect");
        ResolveIndex(m_hOnClientConnected, "ISource2GameClients::OnClientConnected");
        ResolveIndex(m_hSteamAPIActivated, "ISource2Server::GameServerSteamAPIActivated");
        ResolveIndex(m_hSteamAPIDeactivated, "ISource2Server::GameServerSteamAPIDeactivated");
        ResolveIndex(m_hPostEventAbstract, "IGameEventSystem::PostEventAbstract");
        ResolveIndex(m_hOnServerGamePostSimulate, "IGameSystem::OnServerGamePostSimulate");
        ResolveIndex(m_hLoadEventsFromFile, "IGameEventManager2::LoadEventsFromFile");
        ResolveIndex(m_hFireEvent, "IGameEventManager2::FireEvent");
        ResolveIndex(m_hSendNetMessage, "CServerSideClientBase::SendNetMessage");
        ResolveIndex(m_hFilterMessage, "INetworkMessageProcessingPreFilter::FilterMessage");
        ResolveIndex(m_hCheckTransmit, "ISource2GameEntities::CheckTransmit");

        m_hGameFrame->Add(g_pSource2Server);
        m_hStartupServer->Add(g_pNetworkServerService);
        m_hDispatchConCommand->Add(g_pCVar);
        m_hClientCommand->Add(g_pSource2GameClients);
        m_hClientPutInServer->Add(g_pSource2GameClients);
        m_hClientVoice->Add(g_pSource2GameClients);
        m_hClientSettingsChanged->Add(g_pSource2GameClients);
        m_hClientSvcUserMessage->Add(g_pSource2GameClients);
        m_hClientDisconnect->Add(g_pSource2GameClients);
        m_hClientConnect->Add(g_pSource2GameClients);
        m_hOnClientConnected->Add(g_pSource2GameClients);
        m_hSteamAPIActivated->Add(g_pSource2Server);
        m_hSteamAPIDeactivated->Add(g_pSource2Server);
        m_hPostEventAbstract->Add(shared::g_pGameEventSystem);
        m_hCheckTransmit->Add(g_pSource2GameEntities);

        m_pCEntityDebugGameSystemVTable = libserver.GetVirtualTableByName("CEntityDebugGameSystem").GetPtr();
        if (m_pCEntityDebugGameSystemVTable)
            m_hOnServerGamePostSimulate->AddGlobal(reinterpret_cast<IGameSystem*>(&m_pCEntityDebugGameSystemVTable));

        m_pCGameEventManagerVTable = libserver.GetVirtualTableByName("CGameEventManager").GetPtr();
        if (m_pCGameEventManagerVTable)
        {
            m_hLoadEventsFromFile->AddGlobal(reinterpret_cast<IGameEventManager2*>(&m_pCGameEventManagerVTable));
            m_hFireEvent->AddGlobal(reinterpret_cast<IGameEventManager2*>(&m_pCGameEventManagerVTable));
        }

        m_pCServerSideClientVTable = libengine.GetVirtualTableByName("CServerSideClient").GetPtr();
        if (m_pCServerSideClientVTable)
            m_hSendNetMessage->AddGlobal(reinterpret_cast<CServerSideClientBase*>(&m_pCServerSideClientVTable));

        m_pCServerSideClientPreFilterVTable = libengine.GetVirtualTableByBase("CServerSideClient", "INetworkMessageProcessingPreFilter").GetPtr();
        if (m_pCServerSideClientPreFilterVTable)
            m_hFilterMessage->AddGlobal(reinterpret_cast<INetworkMessageProcessingPreFilter*>(&m_pCServerSideClientPreFilterVTable));
    }

    void Virtuals::DestructListeners()
    {
        m_hGameFrame->Remove(g_pSource2Server);
        m_hStartupServer->Remove(g_pNetworkServerService);
        // Already gone when this is the second half of a console "meta unload".
        if (m_hDispatchConCommand)
            m_hDispatchConCommand->Remove(g_pCVar);
        m_hClientCommand->Remove(g_pSource2GameClients);
        m_hClientPutInServer->Remove(g_pSource2GameClients);
        m_hClientVoice->Remove(g_pSource2GameClients);
        m_hClientSettingsChanged->Remove(g_pSource2GameClients);
        m_hClientSvcUserMessage->Remove(g_pSource2GameClients);
        m_hClientDisconnect->Remove(g_pSource2GameClients);
        m_hClientConnect->Remove(g_pSource2GameClients);
        m_hOnClientConnected->Remove(g_pSource2GameClients);
        m_hSteamAPIActivated->Remove(g_pSource2Server);
        m_hSteamAPIDeactivated->Remove(g_pSource2Server);
        m_hPostEventAbstract->Remove(shared::g_pGameEventSystem);
        m_hCheckTransmit->Remove(g_pSource2GameEntities);

        if (m_pCEntityDebugGameSystemVTable)
            m_hOnServerGamePostSimulate->RemoveGlobal(reinterpret_cast<IGameSystem*>(&m_pCEntityDebugGameSystemVTable));

        if (m_pCGameEventManagerVTable)
        {
            m_hLoadEventsFromFile->RemoveGlobal(reinterpret_cast<IGameEventManager2*>(&m_pCGameEventManagerVTable));
            m_hFireEvent->RemoveGlobal(reinterpret_cast<IGameEventManager2*>(&m_pCGameEventManagerVTable));
        }

        if (m_pCServerSideClientVTable)
            m_hSendNetMessage->RemoveGlobal(reinterpret_cast<CServerSideClientBase*>(&m_pCServerSideClientVTable));
        if (m_pCServerSideClientPreFilterVTable)
            m_hFilterMessage->RemoveGlobal(reinterpret_cast<INetworkMessageProcessingPreFilter*>(&m_pCServerSideClientPreFilterVTable));

        delete m_hGameFrame;
        delete m_hStartupServer;
        delete m_hDispatchConCommand;
        delete m_hClientCommand;
        delete m_hClientPutInServer;
        delete m_hClientVoice;
        delete m_hClientSettingsChanged;
        delete m_hClientSvcUserMessage;
        delete m_hClientDisconnect;
        delete m_hClientConnect;
        delete m_hOnClientConnected;
        delete m_hSteamAPIActivated;
        delete m_hSteamAPIDeactivated;
        delete m_hPostEventAbstract;
        delete m_hOnServerGamePostSimulate;
        delete m_hLoadEventsFromFile;
        delete m_hFireEvent;
        delete m_hSendNetMessage;
        delete m_hFilterMessage;
        delete m_hCheckTransmit;

        m_hGameFrame = nullptr;
        m_hStartupServer = nullptr;
        m_hDispatchConCommand = nullptr;
        m_hClientCommand = nullptr;
        m_hClientPutInServer = nullptr;
        m_hClientVoice = nullptr;
        m_hClientSettingsChanged = nullptr;
        m_hClientSvcUserMessage = nullptr;
        m_hClientDisconnect = nullptr;
        m_hClientConnect = nullptr;
        m_hOnClientConnected = nullptr;
        m_hSteamAPIActivated = nullptr;
        m_hSteamAPIDeactivated = nullptr;
        m_hPostEventAbstract = nullptr;
        m_hOnServerGamePostSimulate = nullptr;
        m_hLoadEventsFromFile = nullptr;
        m_hFireEvent = nullptr;
        m_hSendNetMessage = nullptr;
        m_hFilterMessage = nullptr;
        m_hCheckTransmit = nullptr;

        m_pCEntityDebugGameSystemVTable = nullptr;
        m_pCGameEventManagerVTable = nullptr;
        m_pCServerSideClientVTable = nullptr;
        m_pCServerSideClientPreFilterVTable = nullptr;
    }

    // Before the game's frame: the menu-key fakes leave m_hMyWeapons, so no
    // game code ever sees them (slotinput.cpp).
    KHook::Return<void> Virtuals::Hook_GameFramePre(ISource2Server* pThis, bool simulating, bool bFirstTick, bool bLastTick)
    {
        slotinput::Eject();
        return { KHook::Action::Ignore };
    }

    KHook::Return<void> Virtuals::Hook_GameFrame(ISource2Server* pThis, bool simulating, bool bFirstTick, bool bLastTick)
    {
        TK_VPROF("Source2Mod::GameFrame");

        // First, and before the early return below: a signal handler someone
        // replaced has to go back regardless of whether the world is up.
        crashhandler::OnGameFrame();

        scheduler::Tick(simulating);

        // Center HTML menus fade, so the open ones have to be redrawn every
        // frame. MenuManager::Tick() existed but nothing called it, which left
        // a menu on screen for a moment and then gone.
        menus::menuManager.Tick();

        // After the redraw: the key mask of the page now on screen.
        slotinput::OnFrame();

        // Persistent scripts whose entity went away (round restart, map
        // change) are spawned again here, never inside the deletion itself.
        scripts::scriptsManager.OnGameFrame(simulating);

        if (shared::getGlobalVars())
            g_bHasTicked = true;

        // Players Steam validated since the last frame, and permission changes
        // that were held back to be announced from here.
        permissions::permissionsManager.OnGameFrame();

        pluginManager.OnGameFrame(simulating, bFirstTick, bLastTick);

        // Detours that lost their last listener come out here, outside their
        // own dispatch. Plugin unloads never run from here: see
        // PluginManager::RequestUnload.
        gamehooks::gameHooksManager.Tick();

        if (m_bSelfUnloadPending)
        {
            m_bSelfUnloadPending = false;

            // The one hook the coming "meta unload" travels through: deleting
            // it from inside that call would wait for the call to return.
            // Here it is another function's dispatch, so this is a plain
            // synchronous removal, and the re-issued command then reaches
            // metamod with no hook of ours on the stack.
            m_hDispatchConCommand->Remove(g_pCVar);
            delete m_hDispatchConCommand;
            m_hDispatchConCommand = nullptr;

            char cmd[64];
            snprintf(cmd, sizeof(cmd), "meta unload %d\n", g_PLID);
            g_pEngineServer->ServerCommand(cmd);
        }

        return { KHook::Action::Ignore };
    }

    KHook::Return<void> Virtuals::Hook_StartupServer(INetworkServerService* pThis, const GameSessionConfiguration_t& config, ISource2WorldSession* pWorldSession, const char* pszMapName)
    {
        TK_VPROF("Source2Mod::StartupServer");

        crashhandler::OnStartupServer(pszMapName);

        // Re-read every time rather than once: the engine can hand out a new
        // entity system for the next map, and CS2Fixes refreshes it on every
        // StartupServer for the same reason. Keeping the first one would mean
        // a stale pointer and listeners attached to a system nothing uses.
        CGameEntitySystem* pEntitySystem = GameEntitySystem();

        if (pEntitySystem && pEntitySystem != shared::g_pEntitySystem)
        {
            shared::g_pEntitySystem = pEntitySystem;
            pEntitySystem->AddListenerEntity(&entityListener);

            // Plugins register their listeners while they load, which is
            // before the first entity system exists at all.
            entities::entitiesManager.AttachEntityListeners();

            shared::g_bDetoursLoaded = true;
        }

        if (g_bHasTicked)
        {
            scheduler::RemoveMapChangeTimers();
        }

        g_bHasTicked = false;

        pluginManager.OnStartupServer(config, pWorldSession, pszMapName);

        return { KHook::Action::Ignore };
    }

    KHook::Return<void> Virtuals::Hook_ClientPutInServer(ISource2GameClients* pThis, CPlayerSlot slot, const char* pszName, int type, uint64 xuid)
    {
        TK_VPROF("Source2Mod::ClientPutInServer");

        slotinput::ResetPlayer(slot);

        // First: a plugin's OnClientPutInServer may already check permissions.
        permissions::permissionsManager.OnClientPutInServer(slot, type, xuid);

        pluginManager.OnClientPutInServer(slot, pszName, type, xuid);

        return { KHook::Action::Ignore };
    }

    KHook::Return<void> Virtuals::Hook_ClientVoice(ISource2GameClients* pThis, CPlayerSlot slot)
    {
        TK_VPROF("Source2Mod::ClientVoice");

        pluginManager.OnClientVoice(slot);

        return { KHook::Action::Ignore };
    }

    KHook::Return<void> Virtuals::Hook_ClientSettingsChanged(ISource2GameClients* pThis, CPlayerSlot slot)
    {
        TK_VPROF("Source2Mod::ClientSettingsChanged");

        pluginManager.OnClientSettingsChanged(slot);

        return { KHook::Action::Ignore };
    }

    KHook::Return<void> Virtuals::Hook_DispatchConCommand(ICvar* pThis, ConCommandRef cmd, const CCommandContext& ctx, const CCommand& args)
    {
        slotinput::Eject();

        // "meta unload <this plugin>": metamod would call Unload() from inside
        // this very dispatch, and Unload() deleting this hook would then wait
        // for the dispatch to return -- a deadlock the watchdog ends. So the
        // command stops here and is re-issued from the next GameFrame, once
        // this hook is gone (see Hook_GameFrame).
        if (args.ArgC() >= 3 && !V_stricmp(args.Arg(0), "meta") && !V_stricmp(args.Arg(1), "unload") && atoi(args.Arg(2)) == g_PLID)
        {
            if (!m_bSelfUnloadPending)
                FP_INFO("Unloading at the end of the frame (a console unload arrives through a hook of Source2Mod's own)");

            m_bSelfUnloadPending = true;
            return { KHook::Action::Supersede };
        }

        TK_VPROF("Source2Mod::DispatchConCommand");

        if (args.ArgC() >= 2)
        {
            const char* cmdName = args.Arg(0);
            const char* msg = args.Arg(1);

            if (V_strcmp(cmdName, "say") == 0 || V_strcmp(cmdName, "say_team") == 0)
            {
                std::string message = msg;

                if (message.size() >= 2 && message.front() == '"' && message.back() == '"')
                    message = message.substr(1, message.size() - 2);

                std::string prefix;

                bool isPublic =
                    shared::g_pCoreConfig->IsPublicChatTrigger(message, prefix);

                bool isSilent =
                    shared::g_pCoreConfig->IsSilentChatTrigger(message, prefix);

                if (isPublic || isSilent)
                {
                    std::string cleaned = message.substr(prefix.size());

                    CCommand parsed;
                    parsed.Tokenize(cleaned.c_str());

                    if (parsed.ArgC() > 0)
                    {
                        // Where a reply to it goes: back to chat.
                        const Source2ModCommandSource source = isSilent ? Source2ModCommandSource::SilentChat : Source2ModCommandSource::Chat;

                        Action r = commands::DispatchConsoleListener(ctx, parsed, false, source);

                        if (r != Action::Supersede)
                            commands::DispatchConsoleListener(ctx, parsed, true, source);

                        if (r == Action::Supersede)
                            return { KHook::Action::Supersede };
                    }

                    if (isSilent)
                        return { KHook::Action::Supersede };

                    return { KHook::Action::Ignore };
                }
            }
        }

        Action result = commands::DispatchConsoleListener(ctx, args, false);

        // Override still runs the original, so post listeners fire for it too.
        if (result == Action::Supersede)
            return { result };

        commands::DispatchConsoleListener(ctx, args, true);

        return { result };
    }

    KHook::Return<void> Virtuals::Hook_ClientCommand(ISource2GameClients* pThis, CPlayerSlot slot, const CCommand& args)
    {
        TK_VPROF("Source2Mod::ClientCommand");

        slotinput::Eject();

        if (slot != -1 && !V_strncmp(args.Arg(0), "jointeam", 8))
        {
            CCommandContext ctx(CT_NO_TARGET, slot);
            Action result = commands::DispatchConsoleListener(ctx, args, false);
            if (result == Action::Supersede)
                return { result };

            commands::DispatchConsoleListener(ctx, args, true);
        }

        return { KHook::Action::Ignore };
    }

    KHook::Return<void> Virtuals::Hook_ClientSvcUserMessage(ISource2GameClients* pThis, CPlayerSlot slot, int nType, uint32 nSize, const void* pBuffer)
    {
        TK_VPROF("Source2Mod::ClientSvcUserMessage");

        if (nType != static_cast<int>(ECstrike15UserMessages::CS_UM_CustomHudClicked))
            return { KHook::Action::Ignore };

        if (auto* pController = CCSPlayerController::FromSlot(slot))
            customhud::customHudManager.HandleClick(pController, pBuffer, nSize);

        return { KHook::Action::Ignore };
    }

    KHook::Return<void> Virtuals::Hook_GameServerSteamAPIActivated(ISource2Server* pThis)
    {
        TK_VPROF("Source2Mod::GameServerSteamAPIActivated");

        http::httpManager.OnSteamAPIActivated();
        // After the HTTP manager: the crash report waiting for Steam goes out
        // through it.
        crashhandler::OnSteamAPIActivated();

        permissions::permissionsManager.OnSteamAPIActivated();

        pluginManager.OnGameServerSteamAPIActivated();

        return { KHook::Action::Ignore };
    }

    KHook::Return<void> Virtuals::Hook_GameServerSteamAPIDeactivated(ISource2Server* pThis)
    {
        TK_VPROF("Source2Mod::GameServerSteamAPIDeactivated");

        http::httpManager.OnSteamAPIDeactivated();

        permissions::permissionsManager.OnSteamAPIDeactivated();

        pluginManager.OnGameServerSteamAPIDeactivated();

        return { KHook::Action::Ignore };
    }

    KHook::Return<void> Virtuals::Hook_PostEventAbstract(IGameEventSystem* pThis, CSplitScreenSlot nSlot, bool bLocalOnly, int nClientCount, const uint64* clients, INetworkMessageInternal* pEvent, const CNetMessage* pData, unsigned long nSize, NetChannelBufType_t bufType)
    {
        TK_VPROF("Source2Mod::PostEventAbstract");

        if (!pEvent || !pData)
            return { KHook::Action::Ignore };

        NetMessageInfo_t* pInfo = pEvent->GetNetMessageInfo();
        if (!pInfo)
            return { KHook::Action::Ignore };

        uint64_t* pClients = const_cast<uint64_t*>(reinterpret_cast<const uint64_t*>(clients));

        // A sound the game is starting goes to the sound hooks first, decoded;
        // what they leave of it is what the net-message hooks then see.
        if (pInfo->m_MessageId == sounds::GE_SosStartSoundEvent)
        {
            Action soundResult = sounds::soundsManager.DispatchSoundHook(pClients, const_cast<CNetMessage*>(pData));
            if (soundResult == Action::Supersede)
                return { soundResult };
        }

        Action result = networkmessages::DispatchServerHook(pClients, pInfo->m_MessageId, const_cast<CNetMessage*>(pData));

        return { result };
    }

    KHook::Return<bool> Virtuals::Hook_ClientConnect(ISource2GameClients* pThis, CPlayerSlot slot, const char* pszName, uint64 xuid, const char* pszNetworkID, bool unk1, CBufferString* pRejectReason)
    {
        TK_VPROF("Source2Mod::ClientConnect");

        // A plugin that refuses has written the reason; the engine never
        // sees the connection.
        if (!pluginManager.OnClientConnect(slot, pszName, xuid, pszNetworkID, unk1, pRejectReason))
            return { KHook::Action::Supersede, false };

        return { KHook::Action::Ignore };
    }

    KHook::Return<void> Virtuals::Hook_OnClientConnected(ISource2GameClients* pThis, CPlayerSlot slot, const char* pszName, uint64 xuid, const char* pszNetworkID, const char* pszAddress, bool bFakePlayer)
    {
        TK_VPROF("Source2Mod::OnClientConnected");

        pluginManager.OnClientConnected(slot, pszName, xuid, pszNetworkID, pszAddress, bFakePlayer);

        return { KHook::Action::Ignore };
    }

    KHook::Return<void> Virtuals::Hook_ClientDisconnect(ISource2GameClients* pThis, CPlayerSlot slot, ENetworkDisconnectionReason reason, const char* pszName, uint64 xuid, const char* pszNetworkID)
    {
        TK_VPROF("Source2Mod::ClientDisconnect");

        // Pre: the plugins see the player while everything is still there.
        pluginManager.OnClientDisconnect(slot, reason, pszName, xuid, pszNetworkID);

        return { KHook::Action::Ignore };
    }

    KHook::Return<void> Virtuals::Hook_ClientDisconnectPost(ISource2GameClients* pThis, CPlayerSlot slot, ENetworkDisconnectionReason reason, const char* pszName, uint64 xuid, const char* pszNetworkID)
    {
        TK_VPROF("Source2Mod::ClientDisconnectPost");

        sounds::soundsManager.OnClientDisconnect(slot);
        transmit::transmitManager.OnClientDisconnect(slot);
        menus::menuManager.OnClientDisconnect(slot);

        pluginManager.OnClientDisconnectPost(slot, reason, pszName, xuid, pszNetworkID);

        // After the plugins: theirs is the last look at who the player was.
        permissions::permissionsManager.OnClientDisconnect(slot);
        cookies::cookiesManager.OnClientDisconnect(slot);

        return { KHook::Action::Ignore };
    }

    KHook::Return<void> Virtuals::Hook_CheckTransmit(ISource2GameEntities* pThis, CCheckTransmitInfo** ppInfoList, int nInfoCount, CBitVec<16384>& unionTransmitEdicts, CBitVec<16384>& unionTransmitEdicts2, const Entity2Networkable_t** pNetworkables, const uint16* pEntityIndicies, int nEntities)
    {
        TK_VPROF("Source2Mod::CheckTransmit");

        // The menu-key fakes go into m_hMyWeapons for this pack only, and stay
        // off every wire but their owner's.
        slotinput::Inject();
        for (int i = 0; i < nInfoCount; i++)
            slotinput::BlockForViewer(reinterpret_cast<CCheckTransmitInfo*>(ppInfoList[i]));

        transmit::transmitManager.OnCheckTransmit(ppInfoList, nInfoCount, pEntityIndicies, nEntities);

        return { KHook::Action::Ignore };
    }

    KHook::Return<void> Virtuals::Hook_OnServerGamePostSimulate(IGameSystem* pThis, const EventServerGamePostSimulate_t* const pMsg)
    {
        TK_VPROF("Source2Mod::OnServerGamePostSimulate");

        mysql::mysqlManager.RunFrame();
        return { KHook::Action::Ignore };
    }

    KHook::Return<int> Virtuals::Hook_LoadEventsFromFile(IGameEventManager2* pThis, const char* filename, bool bSearchAll)
    {
        TK_VPROF("Source2Mod::LoadEventsFromFile");

        ExecuteOnce(
            shared::g_pGameEventManager = pThis;
            events::InitEvents();
        )

        pluginManager.OnLoadEventsFromFile(pThis, filename, bSearchAll);

        return { KHook::Action::Ignore, 0 };
    }

    KHook::Return<bool> Virtuals::Hook_FireEvent(IGameEventManager2* pThis, IGameEvent* event, bool bDontBroadcast)
    {
        TK_VPROF("Source2Mod::FireEvent");

        if (!event)
            return { KHook::Action::Ignore, false };

        // LoadEventsFromFile is where the manager normally comes from, but it
        // has already run when Source2Mod is loaded after the game's event
        // files were read. The hooked instance is the same object.
        if (!shared::g_pGameEventManager)
        {
            shared::g_pGameEventManager = pThis;
            events::InitEvents();
        }

        bool localDontBroadcast = bDontBroadcast;
        if (!events::DispatchGameEvent(event, false, localDontBroadcast))
        {
            // FireEvent owns the event and frees it; skipping the original
            // means we have to. KHook still runs the post callback, so push
            // an empty slot to keep eventStack paired with nested events.
            shared::g_pGameEventManager->FreeEvent(event);
            eventStack.push_back(nullptr);
            return { KHook::Action::Supersede, false };
        }

        eventStack.push_back(shared::g_pGameEventManager->DuplicateEvent(event));

        if (localDontBroadcast != bDontBroadcast)
        {
            // A listener changed the broadcast flag: carry on down the chain
            // with the new one. Unlike CallOriginal this keeps the original
            // skipped when an earlier hook superseded it (and freed the event),
            // the hooks after this one see the new flag, and the post hooks --
            // ours included, which pops eventStack -- run inside the recall.
            return KHook::Recall(&IGameEventManager2::FireEvent, KHook::Return<bool>{ KHook::Action::Ignore, true }, pThis, event, localDontBroadcast);
        }

        return { KHook::Action::Ignore, true };
    }

    KHook::Return<bool> Virtuals::Hook_FireEventPost(IGameEventManager2* pThis, IGameEvent* event, bool bDontBroadcast)
    {
        TK_VPROF("Source2Mod::FireEventPost");

        if (!event)
            return { KHook::Action::Ignore, false };

        if (!eventStack.empty())
        {
            IGameEvent* copy = eventStack.back();
            eventStack.pop_back();

            if (copy)
            {
                bool dummy = bDontBroadcast;
                events::DispatchGameEvent(copy, true, dummy);
                shared::g_pGameEventManager->FreeEvent(copy);
            }
        }

        return { KHook::Action::Ignore, true };
    }

    KHook::Return<bool> Virtuals::Hook_FilterMessage(INetworkMessageProcessingPreFilter* pThis, const CNetMessage* pData, INetChannel* pChannel)
    {
        TK_VPROF("Source2Mod::FilterMessage");

        // pThis is the client's pre-filter subobject, not the whole client;
        // static_cast steps back to the CServerSideClientBase around it.
        if (!pThis || !pData)
            return { KHook::Action::Ignore, true };

        INetworkMessageInternal* pNetMsg = pData->GetNetMessage();
        if (!pNetMsg)
            return { KHook::Action::Ignore, true };

        NetMessageInfo_t* pInfo = pNetMsg->GetNetMessageInfo();
        if (!pInfo)
            return { KHook::Action::Ignore, true };

        const Action action = networkmessages::DispatchClientHook(static_cast<CServerSideClientBase*>(pThis)->m_nClientSlot, pInfo->m_MessageId, const_cast<CNetMessage*>(pData));

        return { action, true };
    }

    KHook::Return<bool> Virtuals::Hook_SendNetMessage(CServerSideClientBase* pThis, const CNetMessage* pData, NetChannelBufType_t bufType)
    {
        TK_VPROF("Source2Mod::SendNetMessage");

        if (!pThis || !pData)
            return { KHook::Action::Ignore, true };

        INetworkMessageInternal* pNetMsg = pData->GetNetMessage();
        if (!pNetMsg)
            return { KHook::Action::Ignore, true };

        NetMessageInfo_t* pInfo = pNetMsg->GetNetMessageInfo();
        if (!pInfo)
            return { KHook::Action::Ignore, true };

        Action result = networkmessages::DispatchServerInternalHook(pThis->m_nClientSlot, pInfo->m_MessageId, const_cast<CNetMessage*>(pData));

        return { result, true };
    }

    void CEntityListener::OnEntitySpawned(CEntityInstance* pEntity)
    {
    }

    void CEntityListener::OnEntityCreated(CEntityInstance* pEntity)
    {
        TK_VPROF("Source2Mod::OnEntityCreated");

        if (!V_strcmp("cs_gamerules", pEntity->GetClassname()))
            shared::g_pGameRules = static_cast<CCSGameRulesProxy*>(pEntity)->m_pGameRules;
    }

    void CEntityListener::OnEntityDeleted(CEntityInstance* pEntity)
    {
        TK_VPROF("Source2Mod::OnEntityDeleted");

        transmit::transmitManager.OnEntityDeleted(pEntity);
        scripts::scriptsManager.OnEntityDeleted(pEntity);

        // Drop a layout's click callbacks the moment the entity goes, rather
        // than waiting for the next click to notice the handle went stale --
        // the handlers hold plugin code and there may never be another click.
        if (!V_strcmp("custom_hud_layout", pEntity->GetClassname()))
            customhud::customHudManager.UnhookCustomHudClick(static_cast<CCSCustomHudLayout*>(pEntity));
    }

    void CEntityListener::OnEntityParentChanged(CEntityInstance* pEntity, CEntityInstance* pNewParent)
    {
    }
}
