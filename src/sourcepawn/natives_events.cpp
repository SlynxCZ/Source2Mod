/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * The rest of IGameEvent and IGameEventManager2 for SourcePawn
 * (source2mod/events.inc), and the SourceMod events.inc natives that create
 * and fire events. The Event handles themselves, the hooks and the getters
 * and setters both include sets share live in natives.cpp.
 *
 * Project: Source2Mod
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/shared.h"

#include <source2mod/schema/entity/classes/CBasePlayerController.h>
#include <source2mod/schema/entity/classes/CBasePlayerPawn.h>

#include <gameevents.pb.h>
#include <google/protobuf/message.h>
#include <igameevents.h>
#include <networksystem/inetworkmessages.h>

#include <cstdio>
#include <cstdlib>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        PluginId CallerId(IPluginContext* ctx)
        {
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            return plugin ? plugin->Id() : 0;
        }

        IGameEventManager2* Manager(IPluginContext* ctx)
        {
            if (!shared::g_pGameEventManager)
                ctx->ThrowNativeError("The game event manager is not available yet");
            return shared::g_pGameEventManager;
        }

        // An entity argument: NULL_ENTITY (-1) is null; anything else must exist.
        bool EntityArg(IPluginContext* ctx, cell_t entity, CEntityInstance** out)
        {
            bool ok;
            *out = OptionalEntityArg(ctx, entity, &ok);
            return ok;
        }

        cell_t WriteString(IPluginContext* ctx, cell_t addr, cell_t maxlen, const char* value)
        {
            size_t written = 0;
            ctx->StringToLocalUTF8(addr, maxlen, value ? value : "", &written);
            return static_cast<cell_t>(written);
        }

        // ---- IGameEvent ------------------------------------------------------------

        cell_t N_GetID(IPluginContext* ctx, const cell_t* params)
        {
            IGameEvent* ev = ScriptEvent(ctx, params[1]);
            return ev ? ev->GetID() : -1;
        }

        cell_t N_IsReliable(IPluginContext* ctx, const cell_t* params)
        {
            IGameEvent* ev = ScriptEvent(ctx, params[1]);
            return ev && ev->IsReliable();
        }

        cell_t N_IsLocal(IPluginContext* ctx, const cell_t* params)
        {
            IGameEvent* ev = ScriptEvent(ctx, params[1]);
            return ev && ev->IsLocal();
        }

        cell_t N_IsEmpty(IPluginContext* ctx, const cell_t* params)
        {
            IGameEvent* ev = ScriptEvent(ctx, params[1]);
            return !ev || ev->IsEmpty(LocalString(ctx, params[2]));
        }

        cell_t N_HasKey(IPluginContext* ctx, const cell_t* params)
        {
            IGameEvent* ev = ScriptEvent(ctx, params[1]);
            return ev && ev->HasKey(LocalString(ctx, params[2]));
        }

        // GetUint64(key, char[] buffer, int maxlen, const char[] defValue = "0")
        cell_t N_GetUint64(IPluginContext* ctx, const cell_t* params)
        {
            IGameEvent* ev = ScriptEvent(ctx, params[1]);
            if (!ev)
                return 0;
            const uint64 def = std::strtoull(LocalString(ctx, params[5]), nullptr, 10);
            char text[24];
            std::snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(ev->GetUint64(LocalString(ctx, params[2]), def)));
            WriteString(ctx, params[3], params[4], text);
            return 0;
        }

        cell_t N_SetUint64(IPluginContext* ctx, const cell_t* params)
        {
            if (IGameEvent* ev = ScriptEvent(ctx, params[1]))
                ev->SetUint64(LocalString(ctx, params[2]), std::strtoull(LocalString(ctx, params[3]), nullptr, 10));
            return 0;
        }

        cell_t N_GetEHandle(IPluginContext* ctx, const cell_t* params)
        {
            IGameEvent* ev = ScriptEvent(ctx, params[1]);
            return ev ? EntityCell(ev->GetEHandle(LocalString(ctx, params[2]), HandleFromCell(params[3]))) : -1;
        }

        cell_t N_GetEntity(IPluginContext* ctx, const cell_t* params)
        {
            IGameEvent* ev = ScriptEvent(ctx, params[1]);
            CEntityInstance* fallback;
            if (!ev || !EntityArg(ctx, params[3], &fallback))
                return -1;
            return EntityCell(ev->GetEntity(LocalString(ctx, params[2]), fallback));
        }

        cell_t N_GetEntityIndex(IPluginContext* ctx, const cell_t* params)
        {
            IGameEvent* ev = ScriptEvent(ctx, params[1]);
            return ev ? ev->GetEntityIndex(LocalString(ctx, params[2]), CEntityIndex(params[3])).Get() : params[3];
        }

        cell_t N_GetPawnEHandle(IPluginContext* ctx, const cell_t* params)
        {
            IGameEvent* ev = ScriptEvent(ctx, params[1]);
            return ev ? EntityCell(ev->GetPawnEHandle(LocalString(ctx, params[2]))) : -1;
        }

        cell_t N_GetPawnEntityIndex(IPluginContext* ctx, const cell_t* params)
        {
            IGameEvent* ev = ScriptEvent(ctx, params[1]);
            return ev ? ev->GetPawnEntityIndex(LocalString(ctx, params[2])).Get() : -1;
        }

        cell_t N_SetEntity(IPluginContext* ctx, const cell_t* params)
        {
            IGameEvent* ev = ScriptEvent(ctx, params[1]);
            CEntityInstance* entity;
            if (ev && EntityArg(ctx, params[3], &entity))
                ev->SetEntity(LocalString(ctx, params[2]), entity);
            return 0;
        }

        cell_t N_SetEntityHandle(IPluginContext* ctx, const cell_t* params)
        {
            if (IGameEvent* ev = ScriptEvent(ctx, params[1]))
                ev->SetEntity(LocalString(ctx, params[2]), HandleFromCell(params[3]));
            return 0;
        }

        cell_t N_SetPlayerPawn(IPluginContext* ctx, const cell_t* params)
        {
            IGameEvent* ev = ScriptEvent(ctx, params[1]);
            CEntityInstance* pawn;
            if (ev && EntityArg(ctx, params[3], &pawn))
                ev->SetPlayer(LocalString(ctx, params[2]), pawn);
            return 0;
        }

        cell_t N_SetPlayerRaw(IPluginContext* ctx, const cell_t* params)
        {
            IGameEvent* ev = ScriptEvent(ctx, params[1]);
            CEntityInstance* pawn;
            if (ev && EntityArg(ctx, params[4], &pawn))
                ev->SetPlayerRaw(LocalString(ctx, params[2]), LocalString(ctx, params[3]), pawn);
            return 0;
        }

        cell_t N_GetDataKeyCount(IPluginContext* ctx, const cell_t* params)
        {
            IGameEvent* ev = ScriptEvent(ctx, params[1]);
            const KeyValues3* keys = ev ? ev->GetDataKeys() : nullptr;
            return keys ? keys->GetMemberCount() : 0;
        }

        cell_t N_GetDataKeyName(IPluginContext* ctx, const cell_t* params)
        {
            IGameEvent* ev = ScriptEvent(ctx, params[1]);
            const KeyValues3* keys = ev ? ev->GetDataKeys() : nullptr;
            if (!keys)
                return WriteString(ctx, params[3], params[4], "");
            if (params[2] < 0 || params[2] >= keys->GetMemberCount())
                return ctx->ThrowNativeError("Key %d is out of range (%d keys)", params[2], keys->GetMemberCount());
            return WriteString(ctx, params[3], params[4], keys->GetMemberName(params[2]));
        }

        // ---- IGameEventManager2 ----------------------------------------------------

        cell_t N_FireEventClientSide(IPluginContext* ctx, const cell_t* params)
        {
            IGameEventManager2* manager = Manager(ctx);
            IGameEvent* ev = manager ? TakeScriptEvent(ctx, params[1]) : nullptr;
            return ev && manager->FireEventClientSide(ev);
        }

        cell_t N_DuplicateEvent(IPluginContext* ctx, const cell_t* params)
        {
            IGameEventManager2* manager = Manager(ctx);
            IGameEvent* ev = manager ? ScriptEvent(ctx, params[1]) : nullptr;
            IGameEvent* copy = ev ? manager->DuplicateEvent(ev) : nullptr;
            return copy ? NewScriptEvent(copy, CallerId(ctx)) : 0;
        }

        cell_t N_LoadEventsFromFile(IPluginContext* ctx, const cell_t* params)
        {
            IGameEventManager2* manager = Manager(ctx);
            return manager ? manager->LoadEventsFromFile(LocalString(ctx, params[1]), params[2] != 0) : 0;
        }

        cell_t N_LookupEventId(IPluginContext* ctx, const cell_t* params)
        {
            IGameEventManager2* manager = Manager(ctx);
            return manager ? manager->LookupEventId(LocalString(ctx, params[1])) : -1;
        }

        cell_t N_HasEventDescriptor(IPluginContext* ctx, const cell_t* params)
        {
            IGameEventManager2* manager = Manager(ctx);
            return manager && manager->HasEventDescriptor(LocalString(ctx, params[1]));
        }

        cell_t N_PrintEventToString(IPluginContext* ctx, const cell_t* params)
        {
            IGameEventManager2* manager = Manager(ctx);
            IGameEvent* ev = manager ? ScriptEvent(ctx, params[1]) : nullptr;
            if (!ev)
                return 0;
            CUtlString out;
            manager->PrintEventToString(ev, out);
            WriteString(ctx, params[2], params[3], out.Get());
            return 0;
        }

        // The message behind a NetMessage handle, when it is a CMsgSource1LegacyGameEvent.
        CNetMessagePB<CMsgSource1LegacyGameEvent>* LegacyEventMessage(IPluginContext* ctx, cell_t handle)
        {
            auto* pb = static_cast<google::protobuf::Message*>(NetMessagePointer(ctx, handle));
            if (!pb)
                return nullptr;
            if (pb->GetTypeName() != "CMsgSource1LegacyGameEvent")
            {
                ctx->ThrowNativeError("NetMessage %x is a %s, not a CMsgSource1LegacyGameEvent", handle, pb->GetTypeName().c_str());
                return nullptr;
            }
            // A NetMessage handle holds the protobuf part of a CNetMessagePB<T>
            // (as the core's network messages hand them out); the wrapper sits
            // at the same place whatever T is.
            return reinterpret_cast<CNetMessagePB<CMsgSource1LegacyGameEvent>*>(
                static_cast<CNetMessagePB<google::protobuf::Message>*>(pb));
        }

        cell_t N_SerializeEvent(IPluginContext* ctx, const cell_t* params)
        {
            IGameEventManager2* manager = Manager(ctx);
            IGameEvent* ev = manager ? ScriptEvent(ctx, params[1]) : nullptr;
            auto* msg = ev ? LegacyEventMessage(ctx, params[2]) : nullptr;
            return msg && manager->SerializeEvent(ev, msg);
        }

        cell_t N_UnserializeEvent(IPluginContext* ctx, const cell_t* params)
        {
            IGameEventManager2* manager = Manager(ctx);
            auto* msg = manager ? LegacyEventMessage(ctx, params[1]) : nullptr;
            IGameEvent* ev = msg ? manager->UnserializeEvent(*msg) : nullptr;
            return ev ? NewScriptEvent(ev, CallerId(ctx)) : 0;
        }

        // ---- SourceMod events.inc --------------------------------------------------

        const sp_nativeinfo_t g_eventNatives[] = {
            // source2mod/events.inc
            { "IGameEvent.GetID", N_GetID },
            { "IGameEvent.IsReliable", N_IsReliable },
            { "IGameEvent.IsLocal", N_IsLocal },
            { "IGameEvent.IsEmpty", N_IsEmpty },
            { "IGameEvent.HasKey", N_HasKey },
            { "IGameEvent.GetUint64", N_GetUint64 },
            { "IGameEvent.SetUint64", N_SetUint64 },
            { "IGameEvent.GetEHandle", N_GetEHandle },
            { "IGameEvent.GetEntity", N_GetEntity },
            { "IGameEvent.GetEntityIndex", N_GetEntityIndex },
            { "IGameEvent.GetPawnEHandle", N_GetPawnEHandle },
            { "IGameEvent.GetPawnEntityIndex", N_GetPawnEntityIndex },
            { "IGameEvent.SetEntity", N_SetEntity },
            { "IGameEvent.SetEntityHandle", N_SetEntityHandle },
            { "IGameEvent.SetPlayerPawn", N_SetPlayerPawn },
            { "IGameEvent.SetPlayerRaw", N_SetPlayerRaw },
            { "IGameEvent.GetDataKeyCount", N_GetDataKeyCount },
            { "IGameEvent.GetDataKeyName", N_GetDataKeyName },
            { "IGameEventManager2.FireEventClientSide", N_FireEventClientSide },
            { "IGameEventManager2.DuplicateEvent", N_DuplicateEvent },
            { "IGameEventManager2.LoadEventsFromFile", N_LoadEventsFromFile },
            { "IGameEventManager2.LookupEventId", N_LookupEventId },
            { "IGameEventManager2.HasEventDescriptor", N_HasEventDescriptor },
            { "IGameEventManager2.PrintEventToString", N_PrintEventToString },
            { "IGameEventManager2.SerializeEvent", N_SerializeEvent },
            { "IGameEventManager2.UnserializeEvent", N_UnserializeEvent },
            // events.inc
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetEventNatives()
    {
        return g_eventNatives;
    }
}
