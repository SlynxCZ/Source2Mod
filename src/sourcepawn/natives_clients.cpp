/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * SourcePawn natives for source2mod/clients.inc: Source2ModClients on
 * ISource2ModClients.
 *
 * Project: Source2Mod
 */
#include "natives.h"

#include "core/clients.h"

#include <interfaces/interfaces.h>

#include <string>
#include <vector>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        clients::ClientsManager& Clients()
        {
            return clients::clientsManager;
        }

        cell_t ToBuffer(IPluginContext* ctx, cell_t addr, cell_t maxlen, const char* value)
        {
            size_t written = 0;
            ctx->StringToLocalUTF8(addr, maxlen, value ? value : "", &written);
            return static_cast<cell_t>(written);
        }

        ESource2ModAuthId Form(cell_t form)
        {
            return (form >= SOURCE2MOD_AUTHID_ENGINE && form <= SOURCE2MOD_AUTHID_STEAMID64) ? static_cast<ESource2ModAuthId>(form)
                                                                                            : SOURCE2MOD_AUTHID_ENGINE;
        }

        cell_t N_IsConnected(IPluginContext*, const cell_t* params)
        {
            return Clients().IsConnected(CPlayerSlot(params[1]));
        }

        cell_t N_IsInGame(IPluginContext*, const cell_t* params)
        {
            return Clients().IsInGame(CPlayerSlot(params[1]));
        }

        cell_t N_IsFakeClient(IPluginContext*, const cell_t* params)
        {
            return Clients().IsFakeClient(CPlayerSlot(params[1]));
        }

        cell_t N_IsAuthorized(IPluginContext*, const cell_t* params)
        {
            return Clients().IsAuthorized(CPlayerSlot(params[1]));
        }

        // GetAuthId(slot, form, buffer, maxlen, validated)
        cell_t N_GetAuthId(IPluginContext* ctx, const cell_t* params)
        {
            const bool validated = params[0] < 5 || params[5];
            return ToBuffer(ctx, params[3], params[4], Clients().GetAuthId(CPlayerSlot(params[1]), Form(params[2]), validated).c_str());
        }

        cell_t N_GetLatency(IPluginContext*, const cell_t* params)
        {
            return sp_ftoc(Clients().GetLatency(CPlayerSlot(params[1])));
        }

        cell_t N_GetConnectionTime(IPluginContext*, const cell_t* params)
        {
            return sp_ftoc(Clients().GetConnectionTime(CPlayerSlot(params[1])));
        }

        // GetClientConVar(slot, name, buffer, maxlen)
        cell_t N_GetClientConVar(IPluginContext* ctx, const cell_t* params)
        {
            return ToBuffer(ctx, params[3], params[4], Clients().GetClientConVar(CPlayerSlot(params[1]), LocalString(ctx, params[2])).c_str());
        }

        // Kick(slot, format, ...)
        cell_t N_Kick(IPluginContext* ctx, const cell_t* params)
        {
            std::vector<char> message(1024);
            if (!FormatParams(ctx, params, 2, message.data(), message.size()))
                return 0;
            Clients().Kick(CPlayerSlot(params[1]), message.data());
            return 0;
        }

        cell_t N_GetClientCount(IPluginContext*, const cell_t* params)
        {
            return Clients().GetClientCount(params[0] < 1 || params[1]);
        }

        // ConvertSteamId(steamId, form, buffer, maxlen)
        cell_t N_ConvertSteamId(IPluginContext* ctx, const cell_t* params)
        {
            const uint64_t id = clients::ClientsManager::ParseSteamId(LocalString(ctx, params[1]));
            if (!id)
                return 0;
            ToBuffer(ctx, params[3], params[4], clients::ClientsManager::FormatSteamId(id, Form(params[2])).c_str());
            return 1;
        }

        cell_t N_GetUserId(IPluginContext*, const cell_t* params)
        {
            if (!g_pEngineServer || !Clients().IsConnected(CPlayerSlot(params[1])))
                return -1;
            return g_pEngineServer->GetPlayerUserId(CPlayerSlot(params[1])).Get();
        }

        cell_t N_FromUserId(IPluginContext*, const cell_t* params)
        {
            if (!g_pEngineServer)
                return -1;
            for (int slot = 0; slot < 64; slot++)
            {
                if (Clients().IsConnected(CPlayerSlot(slot)) && g_pEngineServer->GetPlayerUserId(CPlayerSlot(slot)).Get() == params[1])
                    return slot;
            }
            return -1;
        }

        cell_t N_FromSteamId(IPluginContext* ctx, const cell_t* params)
        {
            const uint64_t id = clients::ClientsManager::ParseSteamId(LocalString(ctx, params[1]));
            if (!id)
                return -1;
            const std::string want = clients::ClientsManager::FormatSteamId(id, SOURCE2MOD_AUTHID_STEAMID64);
            for (int slot = 0; slot < 64; slot++)
            {
                if (Clients().GetAuthId(CPlayerSlot(slot), SOURCE2MOD_AUTHID_STEAMID64, false) == want)
                    return slot;
            }
            return -1;
        }

        const sp_nativeinfo_t g_clientNatives[] = {
            { "Source2ModClients.IsConnected", N_IsConnected },
            { "Source2ModClients.IsInGame", N_IsInGame },
            { "Source2ModClients.IsFakeClient", N_IsFakeClient },
            { "Source2ModClients.IsAuthorized", N_IsAuthorized },
            { "Source2ModClients.GetAuthId", N_GetAuthId },
            { "Source2ModClients.GetLatency", N_GetLatency },
            { "Source2ModClients.GetConnectionTime", N_GetConnectionTime },
            { "Source2ModClients.GetClientConVar", N_GetClientConVar },
            { "Source2ModClients.Kick", N_Kick },
            { "Source2ModClients.GetClientCount", N_GetClientCount },
            { "Source2ModClients.ConvertSteamId", N_ConvertSteamId },
            { "Source2ModClients.GetUserId", N_GetUserId },
            { "Source2ModClients.FromUserId", N_FromUserId },
            { "Source2ModClients.FromSteamId", N_FromSteamId },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetClientNatives()
    {
        return g_clientNatives;
    }
}
