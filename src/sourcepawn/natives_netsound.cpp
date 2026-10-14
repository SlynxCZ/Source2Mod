/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * Net messages and sounds of the SourcePawn API mirroring the C++ one
 * (source2mod/netmessages.inc, source2mod/sounds.inc), on the core's
 * network messages and sounds managers.
 *
 * A NetMessage handle is either the script's own (allocated, freed with the
 * handle) or lent for a hook (valid inside it); nested messages are handles
 * that go with the one they were taken from. Sounds a script makes are
 * created for the core, not the script, so that its handle -- freed with
 * the script -- is the one thing that destroys them.
 *
 * Project: Source2Mod
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/networkmessages.h"
#include "core/sounds.h"

#include <sp_callable.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

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

        IPluginFunction* Function(IPluginContext* ctx, cell_t id)
        {
            IPluginFunction* fn = ctx->GetFunctionById(static_cast<funcid_t>(id));
            if (!fn)
                ctx->ThrowNativeError("Invalid function id (%X)", id);
            return fn;
        }

        uint64_t ReadMask(IPluginContext* ctx, cell_t addr)
        {
            cell_t* m;
            ctx->LocalToPhysAddr(addr, &m);
            return static_cast<uint64_t>(static_cast<uint32_t>(m[0])) | (static_cast<uint64_t>(static_cast<uint32_t>(m[1])) << 32);
        }

        void WriteMask(cell_t* m, uint64_t mask)
        {
            m[0] = static_cast<cell_t>(static_cast<uint32_t>(mask));
            m[1] = static_cast<cell_t>(static_cast<uint32_t>(mask >> 32));
        }

        ISource2ModNetworkMessages& NM()
        {
            return networkmessages::networkMessagesManager;
        }

        // ---- NetMessage handles ---------------------------------------------------

        struct NetMsg : HandleObject
        {
            void* msg = nullptr;
            bool owned = false;
            PluginId owner = 0;
            std::vector<cell_t> children;   // nested-message handles taken from this one

            ~NetMsg() override
            {
                for (cell_t child : children)
                    Handles().Free(child, 0, true);
                if (owned && msg)
                    NM().DeallocateNetMessage(msg);
            }
        };

        HandleType NetMsgType()
        {
            static const HandleType t = Handles().CreateType("NetMessage", [](void* p) { delete static_cast<NetMsg*>(p); });
            return t;
        }

        // Lent ones: the same object, but a script cannot close them.
        HandleType LentNetMsgType()
        {
            static const HandleType t = Handles().CreateType("NetMessageLent", [](void* p) { delete static_cast<NetMsg*>(p); }, NetMsgType(), false);
            return t;
        }

        cell_t NewNetMsg(void* msg, bool owned, PluginId owner, bool lent)
        {
            auto* obj = new NetMsg();
            obj->msg = msg;
            obj->owned = owned;
            obj->owner = owner;
            const cell_t h = Handles().Create(lent ? LentNetMsgType() : NetMsgType(), obj, owner);
            if (!h)
            {
                obj->owned = false;
                delete obj;
                if (owned)
                    NM().DeallocateNetMessage(msg);
            }
            return h;
        }

        NetMsg* MsgOf(IPluginContext* ctx, cell_t handle)
        {
            auto* m = static_cast<NetMsg*>(ReadHandle(ctx, handle, NetMsgType()));
            if (m && !m->msg)
            {
                ctx->ThrowNativeError("NetMessage %x is empty", handle);
                return nullptr;
            }
            return m;
        }

        cell_t N_AllocateByID(IPluginContext* ctx, const cell_t* params)
        {
            void* msg = NM().AllocateNetMessageByID(params[1]);
            return msg ? NewNetMsg(msg, true, CallerId(ctx), false) : 0;
        }

        cell_t N_AllocateByName(IPluginContext* ctx, const cell_t* params)
        {
            void* msg = NM().AllocateNetMessageByPartialName(LocalString(ctx, params[1]));
            return msg ? NewNetMsg(msg, true, CallerId(ctx), false) : 0;
        }

        cell_t N_Deallocate(IPluginContext* ctx, const cell_t* params)
        {
            if (!ReadHandle(ctx, params[1], NetMsgType()))
                return 0;
            const HandleError err = Handles().Free(params[1], CallerId(ctx));
            if (err != HandleError::None)
                return ReportHandleError(ctx, params[1], err);
            return 0;
        }

        cell_t N_HasField(IPluginContext* ctx, const cell_t* params)
        {
            NetMsg* m = MsgOf(ctx, params[1]);
            return m && NM().HasField(m->msg, LocalString(ctx, params[2]));
        }

        // Scalars: Get(msg, field), GetRepeated(msg, field, index), Set(msg, field, v),
        // SetRepeated(msg, field, index, v), Add(msg, field, v).
#define NETMSG_SCALAR(Name, ToCell, FromCell)                                                                                 \
        cell_t N_Get##Name(IPluginContext* ctx, const cell_t* params)                                                         \
        {                                                                                                                     \
            NetMsg* m = MsgOf(ctx, params[1]);                                                                                \
            return m ? ToCell(NM().Get##Name(m->msg, LocalString(ctx, params[2]))) : 0;                                       \
        }                                                                                                                     \
        cell_t N_GetRepeated##Name(IPluginContext* ctx, const cell_t* params)                                                 \
        {                                                                                                                     \
            NetMsg* m = MsgOf(ctx, params[1]);                                                                                \
            return m ? ToCell(NM().GetRepeated##Name(m->msg, LocalString(ctx, params[2]), params[3])) : 0;                    \
        }                                                                                                                     \
        cell_t N_Set##Name(IPluginContext* ctx, const cell_t* params)                                                         \
        {                                                                                                                     \
            if (NetMsg* m = MsgOf(ctx, params[1]))                                                                            \
                NM().Set##Name(m->msg, LocalString(ctx, params[2]), FromCell(params[3]));                                     \
            return 0;                                                                                                         \
        }                                                                                                                     \
        cell_t N_SetRepeated##Name(IPluginContext* ctx, const cell_t* params)                                                 \
        {                                                                                                                     \
            if (NetMsg* m = MsgOf(ctx, params[1]))                                                                            \
                NM().SetRepeated##Name(m->msg, LocalString(ctx, params[2]), params[3], FromCell(params[4]));                  \
            return 0;                                                                                                         \
        }                                                                                                                     \
        cell_t N_Add##Name(IPluginContext* ctx, const cell_t* params)                                                         \
        {                                                                                                                     \
            if (NetMsg* m = MsgOf(ctx, params[1]))                                                                            \
                NM().Add##Name(m->msg, LocalString(ctx, params[2]), FromCell(params[3]));                                     \
            return 0;                                                                                                         \
        }

        cell_t IntToCell(int v) { return static_cast<cell_t>(v); }
        cell_t UIntToCell(uint32_t v) { return static_cast<cell_t>(v); }
        cell_t BoolToCell(bool v) { return v ? 1 : 0; }
        cell_t FloatToCell(float v) { return sp_ftoc(v); }
        cell_t DoubleToCell(double v) { return sp_ftoc(static_cast<float>(v)); }
        int CellToInt(cell_t v) { return v; }
        uint32_t CellToUInt(cell_t v) { return static_cast<uint32_t>(v); }
        bool CellToBool(cell_t v) { return v != 0; }
        float CellToFloat(cell_t v) { return sp_ctof(v); }
        double CellToDouble(cell_t v) { return sp_ctof(v); }

        NETMSG_SCALAR(Int32, IntToCell, CellToInt)
        NETMSG_SCALAR(UInt32, UIntToCell, CellToUInt)
        NETMSG_SCALAR(Bool, BoolToCell, CellToBool)
        NETMSG_SCALAR(Float, FloatToCell, CellToFloat)
        NETMSG_SCALAR(Double, DoubleToCell, CellToDouble)
#undef NETMSG_SCALAR

        // 64-bit fields as decimal text.
        cell_t N_GetInt64(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                ctx->StringToLocalUTF8(params[3], params[4], std::to_string(NM().GetInt64(m->msg, LocalString(ctx, params[2]))).c_str(), nullptr);
            return 0;
        }

        cell_t N_SetInt64(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().SetInt64(m->msg, LocalString(ctx, params[2]), strtoll(LocalString(ctx, params[3]), nullptr, 10));
            return 0;
        }

        cell_t N_AddInt64(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().AddInt64(m->msg, LocalString(ctx, params[2]), strtoll(LocalString(ctx, params[3]), nullptr, 10));
            return 0;
        }

        cell_t N_GetUInt64(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                ctx->StringToLocalUTF8(params[3], params[4], std::to_string(NM().GetUInt64(m->msg, LocalString(ctx, params[2]))).c_str(), nullptr);
            return 0;
        }

        cell_t N_SetUInt64(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().SetUInt64(m->msg, LocalString(ctx, params[2]), strtoull(LocalString(ctx, params[3]), nullptr, 10));
            return 0;
        }

        cell_t N_AddUInt64(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().AddUInt64(m->msg, LocalString(ctx, params[2]), strtoull(LocalString(ctx, params[3]), nullptr, 10));
            return 0;
        }

        cell_t N_GetString(IPluginContext* ctx, const cell_t* params)
        {
            NetMsg* m = MsgOf(ctx, params[1]);
            if (!m)
                return 0;
            size_t written = 0;
            ctx->StringToLocalUTF8(params[3], params[4], NM().GetString(m->msg, LocalString(ctx, params[2])).c_str(), &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_GetRepeatedString(IPluginContext* ctx, const cell_t* params)
        {
            NetMsg* m = MsgOf(ctx, params[1]);
            if (!m)
                return 0;
            size_t written = 0;
            ctx->StringToLocalUTF8(params[4], params[5], NM().GetRepeatedString(m->msg, LocalString(ctx, params[2]), params[3]).c_str(), &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_SetString(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().SetString(m->msg, LocalString(ctx, params[2]), LocalString(ctx, params[3]));
            return 0;
        }

        cell_t N_SetRepeatedString(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().SetRepeatedString(m->msg, LocalString(ctx, params[2]), params[3], LocalString(ctx, params[4]));
            return 0;
        }

        cell_t N_AddString(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().AddString(m->msg, LocalString(ctx, params[2]), LocalString(ctx, params[3]));
            return 0;
        }

        void ToFloats(IPluginContext* ctx, cell_t addr, const float* v, int n)
        {
            cell_t* out;
            ctx->LocalToPhysAddr(addr, &out);
            for (int i = 0; i < n; i++)
                out[i] = sp_ftoc(v[i]);
        }

        void FromFloats(IPluginContext* ctx, cell_t addr, float* v, int n)
        {
            cell_t* in;
            ctx->LocalToPhysAddr(addr, &in);
            for (int i = 0; i < n; i++)
                v[i] = sp_ctof(in[i]);
        }

        cell_t N_GetVector(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
            {
                const Vector v = NM().GetVector(m->msg, LocalString(ctx, params[2]));
                const float f[3] = { v.x, v.y, v.z };
                ToFloats(ctx, params[3], f, 3);
            }
            return 0;
        }

        cell_t N_SetVector(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
            {
                float f[3];
                FromFloats(ctx, params[3], f, 3);
                NM().SetVector(m->msg, LocalString(ctx, params[2]), Vector(f[0], f[1], f[2]));
            }
            return 0;
        }

        cell_t N_AddVector(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
            {
                float f[3];
                FromFloats(ctx, params[3], f, 3);
                NM().AddVector(m->msg, LocalString(ctx, params[2]), Vector(f[0], f[1], f[2]));
            }
            return 0;
        }

        cell_t N_GetQAngle(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
            {
                const QAngle a = NM().GetQAngle(m->msg, LocalString(ctx, params[2]));
                const float f[3] = { a.x, a.y, a.z };
                ToFloats(ctx, params[3], f, 3);
            }
            return 0;
        }

        cell_t N_SetQAngle(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
            {
                float f[3];
                FromFloats(ctx, params[3], f, 3);
                NM().SetQAngle(m->msg, LocalString(ctx, params[2]), QAngle(f[0], f[1], f[2]));
            }
            return 0;
        }

        cell_t N_GetVector2D(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
            {
                const Vector2D v = NM().GetVector2D(m->msg, LocalString(ctx, params[2]));
                const float f[2] = { v.x, v.y };
                ToFloats(ctx, params[3], f, 2);
            }
            return 0;
        }

        cell_t N_SetVector2D(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
            {
                float f[2];
                FromFloats(ctx, params[3], f, 2);
                NM().SetVector2D(m->msg, LocalString(ctx, params[2]), Vector2D(f[0], f[1]));
            }
            return 0;
        }

        cell_t N_GetColor(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
            {
                const Color c = NM().GetColor(m->msg, LocalString(ctx, params[2]));
                cell_t* out;
                ctx->LocalToPhysAddr(params[3], &out);
                out[0] = c.r();
                out[1] = c.g();
                out[2] = c.b();
                out[3] = c.a();
            }
            return 0;
        }

        cell_t N_SetColor(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
            {
                cell_t* in;
                ctx->LocalToPhysAddr(params[3], &in);
                NM().SetColor(m->msg, LocalString(ctx, params[2]), Color(in[0], in[1], in[2], in[3]));
            }
            return 0;
        }

        // GetBytes(msg, field, char[] buffer, maxlen): raw bytes
        cell_t N_GetBytes(IPluginContext* ctx, const cell_t* params)
        {
            NetMsg* m = MsgOf(ctx, params[1]);
            if (!m || params[4] <= 0)
                return 0;
            // The interface writes the whole field; size it first.
            std::vector<uint8_t> tmp(65536);
            const int n = NM().GetBytes(tmp.data(), m->msg, LocalString(ctx, params[2]));
            char* dest;
            ctx->LocalToString(params[3], &dest);
            const int copy = std::min(n, params[4]);
            memcpy(dest, tmp.data(), static_cast<size_t>(std::max(copy, 0)));
            return copy;
        }

        cell_t N_SetBytes(IPluginContext* ctx, const cell_t* params)
        {
            NetMsg* m = MsgOf(ctx, params[1]);
            if (!m)
                return 0;
            char* src;
            ctx->LocalToString(params[3], &src);
            NM().SetBytes(m->msg, LocalString(ctx, params[2]), src, params[4]);
            return 0;
        }

        cell_t Nested(IPluginContext* ctx, cell_t parentHandle, void* nested)
        {
            auto* parent = static_cast<NetMsg*>(Handles().Read(parentHandle, NetMsgType()));
            if (!parent || !nested)
                return 0;
            const cell_t h = NewNetMsg(nested, false, parent->owner, true);
            if (h)
                parent->children.push_back(h);
            return h;
        }

        cell_t N_GetNested(IPluginContext* ctx, const cell_t* params)
        {
            NetMsg* m = MsgOf(ctx, params[1]);
            return m ? Nested(ctx, params[1], NM().GetNestedMessage(m->msg, LocalString(ctx, params[2]))) : 0;
        }

        cell_t N_GetRepeatedNested(IPluginContext* ctx, const cell_t* params)
        {
            NetMsg* m = MsgOf(ctx, params[1]);
            return m ? Nested(ctx, params[1], NM().GetRepeatedNestedMessage(m->msg, LocalString(ctx, params[2]), params[3])) : 0;
        }

        cell_t N_AddNested(IPluginContext* ctx, const cell_t* params)
        {
            NetMsg* m = MsgOf(ctx, params[1]);
            return m ? Nested(ctx, params[1], NM().AddNestedMessage(m->msg, LocalString(ctx, params[2]))) : 0;
        }

        cell_t N_GetRepeatedFieldSize(IPluginContext* ctx, const cell_t* params)
        {
            NetMsg* m = MsgOf(ctx, params[1]);
            return m ? NM().GetRepeatedFieldSize(m->msg, LocalString(ctx, params[2])) : 0;
        }

        cell_t N_ClearRepeatedField(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().ClearRepeatedField(m->msg, LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_Clear(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().Clear(m->msg);
            return 0;
        }

        cell_t N_SendMessage(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().SendMessage(m->msg, params[2], CPlayerSlot(params[3]));
            return 0;
        }

        cell_t N_SendMessageToPlayers(IPluginContext* ctx, const cell_t* params)
        {
            if (NetMsg* m = MsgOf(ctx, params[1]))
                NM().SendMessageToPlayers(m->msg, params[2], ReadMask(ctx, params[3]));
            return 0;
        }

        // Hook handlers: the message lent as a handle for the call.
        struct LentMsg
        {
            cell_t handle;
            LentMsg(void* msg, PluginId owner) : handle(NewNetMsg(msg, false, owner, true)) {}
            ~LentMsg() { Handles().Free(handle, 0, true); }
        };

        Action ToAction(cell_t res)
        {
            return static_cast<Action>(std::clamp<cell_t>(res, 0, 2));
        }

        cell_t N_HookServerMessage(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[1]);
            if (!fn)
                return 0;
            const PluginId owner = CallerId(ctx);
            NetMessageServerHook handler([fn, owner](uint64_t* clients, int messageId, void* msg) -> Action
            {
                LentMsg lent(msg, owner);
                cell_t mask[2];
                WriteMask(mask, *clients);
                sp::CallArgs args;
                args.PushArray(mask, 2, SM_PARAM_COPYBACK);
                args.PushCell(messageId);
                args.PushCell(lent.handle);
                cell_t res = 0;
                Invoke(fn, args, &res);
                *clients = static_cast<uint64_t>(static_cast<uint32_t>(mask[0])) | (static_cast<uint64_t>(static_cast<uint32_t>(mask[1])) << 32);
                return ToAction(res);
            });
            return static_cast<cell_t>(networkmessages::networkMessagesManager.HookServerMessage(owner, std::move(handler)));
        }

        NetMessageClientHook ClientHook(IPluginFunction* fn, PluginId owner)
        {
            return NetMessageClientHook([fn, owner](CPlayerSlot slot, int messageId, void* msg) -> Action
            {
                LentMsg lent(msg, owner);
                sp::CallArgs args;
                args.PushCell(slot.Get());
                args.PushCell(messageId);
                args.PushCell(lent.handle);
                cell_t res = 0;
                Invoke(fn, args, &res);
                return ToAction(res);
            });
        }

        cell_t N_HookClientMessage(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[1]);
            if (!fn)
                return 0;
            const PluginId owner = CallerId(ctx);
            return static_cast<cell_t>(networkmessages::networkMessagesManager.HookClientMessage(owner, ClientHook(fn, owner)));
        }

        cell_t N_HookServerInternalMessage(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[1]);
            if (!fn)
                return 0;
            const PluginId owner = CallerId(ctx);
            return static_cast<cell_t>(networkmessages::networkMessagesManager.HookServerInternalMessage(owner, ClientHook(fn, owner)));
        }

        cell_t N_UnhookNetMessage(IPluginContext*, const cell_t* params)
        {
            return networkmessages::networkMessagesManager.UnhookNetMessage(static_cast<Source2ModHookId>(params[1]));
        }

        // ---- sounds -----------------------------------------------------------------

        ISource2ModSounds& SM()
        {
            return sounds::soundsManager;
        }

        HandleType SoundType()
        {
            static const HandleType t = Handles().CreateType("ISource2ModSound", [](void* p)
            {
                sounds::soundsManager.DestroySound(static_cast<ISource2ModSound*>(p));
            });
            return t;
        }

        // The one a sound hook is handed: not the script's to destroy.
        HandleType LentSoundType()
        {
            static const HandleType t = Handles().CreateType("ISource2ModSoundLent", nullptr, SoundType(), false);
            return t;
        }

        ISource2ModSound* SoundOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<ISource2ModSound*>(ReadHandle(ctx, handle, SoundType()));
        }

        cell_t N_SoundNew(IPluginContext* ctx, const cell_t* params)
        {
            const char* name = LocalString(ctx, params[1]);
            ISource2ModSound* sound = SM().CreateSound(0, *name ? name : nullptr);
            if (!sound)
                return 0;
            const cell_t h = Handles().Create(SoundType(), sound, CallerId(ctx));
            if (!h)
                SM().DestroySound(sound);
            return h;
        }

        cell_t N_SoundSetName(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->SetName(LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_SoundGetName(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            size_t written = 0;
            if (s)
                ctx->StringToLocalUTF8(params[2], params[3], s->GetName() ? s->GetName() : "", &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_SoundSetNameHash(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->SetNameHash(static_cast<uint32_t>(params[2]));
            return 0;
        }

        cell_t N_SoundGetNameHash(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            return s ? static_cast<cell_t>(s->GetNameHash()) : 0;
        }

        cell_t N_SoundGetGuid(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            return s ? static_cast<cell_t>(s->GetGuid()) : 0;
        }

        cell_t N_SoundSetSourceEntityIndex(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->SetSourceEntityIndex(params[2]);
            return 0;
        }

        cell_t N_SoundGetSourceEntityIndex(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            return s ? s->GetSourceEntityIndex() : 0;
        }

        cell_t N_SoundSetSourceEntity(IPluginContext* ctx, const cell_t* params)
        {
            // An entity is its index; SetSourceEntityIndex says the same.
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->SetSourceEntityIndex(params[2]);
            return 0;
        }

        cell_t N_SoundSetPosition(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
            {
                float f[3];
                FromFloats(ctx, params[2], f, 3);
                s->SetPosition(Vector(f[0], f[1], f[2]));
            }
            return 0;
        }

        cell_t N_SoundSetVolume(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->SetVolume(sp_ctof(params[2]));
            return 0;
        }

        cell_t N_SoundGetVolume(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            return s ? sp_ftoc(s->GetVolume()) : 0;
        }

        cell_t N_SoundSetPitch(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->SetPitch(sp_ctof(params[2]));
            return 0;
        }

        cell_t N_SoundGetPitch(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            return s ? sp_ftoc(s->GetPitch()) : 0;
        }

        cell_t N_SoundSetChannel(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->SetChannel(LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_SoundGetChannel(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            size_t written = 0;
            if (s)
                ctx->StringToLocalUTF8(params[2], params[3], s->GetChannel() ? s->GetChannel() : "", &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_SoundHasParam(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            return s && s->HasParam(LocalString(ctx, params[2]));
        }

        cell_t N_SoundGetParamType(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            return s ? static_cast<cell_t>(s->GetParamType(LocalString(ctx, params[2]))) : 0;
        }

        cell_t N_SoundRemoveParam(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->RemoveParam(LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_SoundClearParams(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->ClearParams();
            return 0;
        }

        cell_t N_SoundSetBool(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->SetBool(LocalString(ctx, params[2]), params[3] != 0);
            return 0;
        }

        cell_t N_SoundSetInt32(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->SetInt32(LocalString(ctx, params[2]), params[3]);
            return 0;
        }

        cell_t N_SoundSetUInt32(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->SetUInt32(LocalString(ctx, params[2]), static_cast<uint32_t>(params[3]));
            return 0;
        }

        cell_t N_SoundSetFloat(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->SetFloat(LocalString(ctx, params[2]), sp_ctof(params[3]));
            return 0;
        }

        cell_t N_SoundSetFloat3(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
            {
                float f[3];
                FromFloats(ctx, params[3], f, 3);
                s->SetFloat3(LocalString(ctx, params[2]), Vector(f[0], f[1], f[2]));
            }
            return 0;
        }

        cell_t N_SoundGetBool(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            return s && s->GetBool(LocalString(ctx, params[2]), params[3] != 0);
        }

        cell_t N_SoundGetInt32(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            return s ? s->GetInt32(LocalString(ctx, params[2]), params[3]) : 0;
        }

        cell_t N_SoundGetUInt32(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            return s ? static_cast<cell_t>(s->GetUInt32(LocalString(ctx, params[2]), static_cast<uint32_t>(params[3]))) : 0;
        }

        cell_t N_SoundGetFloat(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            return s ? sp_ftoc(s->GetFloat(LocalString(ctx, params[2]), sp_ctof(params[3]))) : 0;
        }

        cell_t N_SoundGetFloat3(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
            {
                const Vector v = s->GetFloat3(LocalString(ctx, params[2]));
                const float f[3] = { v.x, v.y, v.z };
                ToFloats(ctx, params[3], f, 3);
            }
            return 0;
        }

        cell_t N_SoundAddRecipient(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->AddRecipient(CPlayerSlot(params[2]));
            return 0;
        }

        cell_t N_SoundRemoveRecipient(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->RemoveRecipient(CPlayerSlot(params[2]));
            return 0;
        }

        cell_t N_SoundHasRecipient(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            return s && s->HasRecipient(CPlayerSlot(params[2]));
        }

        cell_t N_SoundAddAllRecipients(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->AddAllRecipients();
            return 0;
        }

        cell_t N_SoundClearRecipients(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->ClearRecipients();
            return 0;
        }

        cell_t N_SoundSetRecipients(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
                s->SetRecipients(ReadMask(ctx, params[2]));
            return 0;
        }

        cell_t N_SoundGetRecipients(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[1]))
            {
                cell_t* m;
                ctx->LocalToPhysAddr(params[2], &m);
                WriteMask(m, s->GetRecipients());
            }
            return 0;
        }

        cell_t N_SoundEmit(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSound* s = SoundOf(ctx, params[1]);
            return s ? static_cast<cell_t>(s->Emit()) : 0;
        }

        const char* Channel(IPluginContext* ctx, cell_t addr)
        {
            const char* c = LocalString(ctx, addr);
            return *c ? c : nullptr;
        }

        // EmitSoundToPlayer(slot, name, volume = 1.0, pitch = 1.0, channel = "")
        cell_t N_EmitToPlayer(IPluginContext* ctx, const cell_t* params)
        {
            return static_cast<cell_t>(SM().EmitSoundToPlayer(CPlayerSlot(params[1]), LocalString(ctx, params[2]), sp_ctof(params[3]),
                                                             sp_ctof(params[4]), Channel(ctx, params[5])));
        }

        cell_t N_EmitToPlayers(IPluginContext* ctx, const cell_t* params)
        {
            return static_cast<cell_t>(SM().EmitSoundToPlayers(ReadMask(ctx, params[1]), LocalString(ctx, params[2]), sp_ctof(params[3]),
                                                              sp_ctof(params[4]), Channel(ctx, params[5])));
        }

        cell_t N_EmitToAll(IPluginContext* ctx, const cell_t* params)
        {
            return static_cast<cell_t>(SM().EmitSoundToAll(LocalString(ctx, params[1]), sp_ctof(params[2]), sp_ctof(params[3]), Channel(ctx, params[4])));
        }

        cell_t N_StopSound(IPluginContext*, const cell_t* params)
        {
            SM().StopSound(static_cast<SoundGuid>(params[1]));
            return 0;
        }

        cell_t N_StopSoundForPlayers(IPluginContext* ctx, const cell_t* params)
        {
            SM().StopSoundForPlayers(static_cast<SoundGuid>(params[1]), ReadMask(ctx, params[2]));
            return 0;
        }

        cell_t N_StopSoundByName(IPluginContext* ctx, const cell_t* params)
        {
            SM().StopSoundByName(LocalString(ctx, params[1]), params[2]);
            return 0;
        }

        cell_t N_SetSoundVolume(IPluginContext*, const cell_t* params)
        {
            SM().SetSoundVolume(static_cast<SoundGuid>(params[1]), sp_ctof(params[2]));
            return 0;
        }

        cell_t N_UpdateSoundParams(IPluginContext* ctx, const cell_t* params)
        {
            if (ISource2ModSound* s = SoundOf(ctx, params[2]))
                SM().UpdateSoundParams(static_cast<SoundGuid>(params[1]), s);
            return 0;
        }

        cell_t N_HookSound(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[1]);
            if (!fn)
                return 0;
            const PluginId owner = CallerId(ctx);
            SoundHook handler([fn, owner](ISource2ModSound* sound) -> Action
            {
                const cell_t h = Handles().Create(LentSoundType(), sound, owner);
                sp::CallArgs args;
                args.PushCell(h);
                cell_t res = 0;
                Invoke(fn, args, &res);
                Handles().Free(h, 0, true);
                return ToAction(res);
            });
            return static_cast<cell_t>(sounds::soundsManager.HookSound(owner, std::move(handler)));
        }

        cell_t N_UnhookSound(IPluginContext*, const cell_t* params)
        {
            return sounds::soundsManager.UnhookSoundId(static_cast<Source2ModHookId>(params[1]));
        }

        cell_t N_HashSoundName(IPluginContext* ctx, const cell_t* params)
        {
            return static_cast<cell_t>(SM().HashSoundName(LocalString(ctx, params[1])));
        }

        const sp_nativeinfo_t g_netSoundNatives[] = {
            // source2mod/netmessages.inc
            { "Source2ModNetworkMessages.AllocateNetMessageByID", N_AllocateByID },
            { "Source2ModNetworkMessages.AllocateNetMessageByPartialName", N_AllocateByName },
            { "Source2ModNetworkMessages.DeallocateNetMessage", N_Deallocate },
            { "Source2ModNetworkMessages.HasField", N_HasField },
            { "Source2ModNetworkMessages.GetInt32", N_GetInt32 },
            { "Source2ModNetworkMessages.GetRepeatedInt32", N_GetRepeatedInt32 },
            { "Source2ModNetworkMessages.SetInt32", N_SetInt32 },
            { "Source2ModNetworkMessages.SetRepeatedInt32", N_SetRepeatedInt32 },
            { "Source2ModNetworkMessages.AddInt32", N_AddInt32 },
            { "Source2ModNetworkMessages.GetUInt32", N_GetUInt32 },
            { "Source2ModNetworkMessages.GetRepeatedUInt32", N_GetRepeatedUInt32 },
            { "Source2ModNetworkMessages.SetUInt32", N_SetUInt32 },
            { "Source2ModNetworkMessages.SetRepeatedUInt32", N_SetRepeatedUInt32 },
            { "Source2ModNetworkMessages.AddUInt32", N_AddUInt32 },
            { "Source2ModNetworkMessages.GetInt64", N_GetInt64 },
            { "Source2ModNetworkMessages.SetInt64", N_SetInt64 },
            { "Source2ModNetworkMessages.AddInt64", N_AddInt64 },
            { "Source2ModNetworkMessages.GetUInt64", N_GetUInt64 },
            { "Source2ModNetworkMessages.SetUInt64", N_SetUInt64 },
            { "Source2ModNetworkMessages.AddUInt64", N_AddUInt64 },
            { "Source2ModNetworkMessages.GetBool", N_GetBool },
            { "Source2ModNetworkMessages.GetRepeatedBool", N_GetRepeatedBool },
            { "Source2ModNetworkMessages.SetBool", N_SetBool },
            { "Source2ModNetworkMessages.SetRepeatedBool", N_SetRepeatedBool },
            { "Source2ModNetworkMessages.AddBool", N_AddBool },
            { "Source2ModNetworkMessages.GetFloat", N_GetFloat },
            { "Source2ModNetworkMessages.GetRepeatedFloat", N_GetRepeatedFloat },
            { "Source2ModNetworkMessages.SetFloat", N_SetFloat },
            { "Source2ModNetworkMessages.SetRepeatedFloat", N_SetRepeatedFloat },
            { "Source2ModNetworkMessages.AddFloat", N_AddFloat },
            { "Source2ModNetworkMessages.GetDouble", N_GetDouble },
            { "Source2ModNetworkMessages.SetDouble", N_SetDouble },
            { "Source2ModNetworkMessages.AddDouble", N_AddDouble },
            { "Source2ModNetworkMessages.GetString", N_GetString },
            { "Source2ModNetworkMessages.GetRepeatedString", N_GetRepeatedString },
            { "Source2ModNetworkMessages.SetString", N_SetString },
            { "Source2ModNetworkMessages.SetRepeatedString", N_SetRepeatedString },
            { "Source2ModNetworkMessages.AddString", N_AddString },
            { "Source2ModNetworkMessages.GetVector", N_GetVector },
            { "Source2ModNetworkMessages.SetVector", N_SetVector },
            { "Source2ModNetworkMessages.AddVector", N_AddVector },
            { "Source2ModNetworkMessages.GetQAngle", N_GetQAngle },
            { "Source2ModNetworkMessages.SetQAngle", N_SetQAngle },
            { "Source2ModNetworkMessages.GetVector2D", N_GetVector2D },
            { "Source2ModNetworkMessages.SetVector2D", N_SetVector2D },
            { "Source2ModNetworkMessages.GetColor", N_GetColor },
            { "Source2ModNetworkMessages.SetColor", N_SetColor },
            { "Source2ModNetworkMessages.GetBytes", N_GetBytes },
            { "Source2ModNetworkMessages.SetBytes", N_SetBytes },
            { "Source2ModNetworkMessages.GetNestedMessage", N_GetNested },
            { "Source2ModNetworkMessages.GetRepeatedNestedMessage", N_GetRepeatedNested },
            { "Source2ModNetworkMessages.AddNestedMessage", N_AddNested },
            { "Source2ModNetworkMessages.GetRepeatedFieldSize", N_GetRepeatedFieldSize },
            { "Source2ModNetworkMessages.ClearRepeatedField", N_ClearRepeatedField },
            { "Source2ModNetworkMessages.Clear", N_Clear },
            { "Source2ModNetworkMessages.SendMessage", N_SendMessage },
            { "Source2ModNetworkMessages.SendMessageToPlayers", N_SendMessageToPlayers },
            { "Source2ModNetworkMessages.HookServerMessage", N_HookServerMessage },
            { "Source2ModNetworkMessages.UnhookServerMessage", N_UnhookNetMessage },
            { "Source2ModNetworkMessages.HookClientMessage", N_HookClientMessage },
            { "Source2ModNetworkMessages.UnhookClientMessage", N_UnhookNetMessage },
            { "Source2ModNetworkMessages.HookServerInternalMessage", N_HookServerInternalMessage },
            { "Source2ModNetworkMessages.UnhookServerInternalMessage", N_UnhookNetMessage },
            // source2mod/sounds.inc
            { "ISource2ModSound.New", N_SoundNew },
            { "ISource2ModSound.SetName", N_SoundSetName },
            { "ISource2ModSound.GetName", N_SoundGetName },
            { "ISource2ModSound.SetNameHash", N_SoundSetNameHash },
            { "ISource2ModSound.GetNameHash", N_SoundGetNameHash },
            { "ISource2ModSound.GetGuid", N_SoundGetGuid },
            { "ISource2ModSound.SetSourceEntityIndex", N_SoundSetSourceEntityIndex },
            { "ISource2ModSound.GetSourceEntityIndex", N_SoundGetSourceEntityIndex },
            { "ISource2ModSound.SetSourceEntity", N_SoundSetSourceEntity },
            { "ISource2ModSound.SetPosition", N_SoundSetPosition },
            { "ISource2ModSound.SetVolume", N_SoundSetVolume },
            { "ISource2ModSound.GetVolume", N_SoundGetVolume },
            { "ISource2ModSound.SetPitch", N_SoundSetPitch },
            { "ISource2ModSound.GetPitch", N_SoundGetPitch },
            { "ISource2ModSound.SetChannel", N_SoundSetChannel },
            { "ISource2ModSound.GetChannel", N_SoundGetChannel },
            { "ISource2ModSound.HasParam", N_SoundHasParam },
            { "ISource2ModSound.GetParamType", N_SoundGetParamType },
            { "ISource2ModSound.RemoveParam", N_SoundRemoveParam },
            { "ISource2ModSound.ClearParams", N_SoundClearParams },
            { "ISource2ModSound.SetBool", N_SoundSetBool },
            { "ISource2ModSound.SetInt32", N_SoundSetInt32 },
            { "ISource2ModSound.SetUInt32", N_SoundSetUInt32 },
            { "ISource2ModSound.SetFloat", N_SoundSetFloat },
            { "ISource2ModSound.SetFloat3", N_SoundSetFloat3 },
            { "ISource2ModSound.GetBool", N_SoundGetBool },
            { "ISource2ModSound.GetInt32", N_SoundGetInt32 },
            { "ISource2ModSound.GetUInt32", N_SoundGetUInt32 },
            { "ISource2ModSound.GetFloat", N_SoundGetFloat },
            { "ISource2ModSound.GetFloat3", N_SoundGetFloat3 },
            { "ISource2ModSound.AddRecipient", N_SoundAddRecipient },
            { "ISource2ModSound.RemoveRecipient", N_SoundRemoveRecipient },
            { "ISource2ModSound.HasRecipient", N_SoundHasRecipient },
            { "ISource2ModSound.AddAllRecipients", N_SoundAddAllRecipients },
            { "ISource2ModSound.ClearRecipients", N_SoundClearRecipients },
            { "ISource2ModSound.SetRecipients", N_SoundSetRecipients },
            { "ISource2ModSound.GetRecipients", N_SoundGetRecipients },
            { "ISource2ModSound.Emit", N_SoundEmit },
            { "Source2ModSounds.EmitSoundToPlayer", N_EmitToPlayer },
            { "Source2ModSounds.EmitSoundToPlayers", N_EmitToPlayers },
            { "Source2ModSounds.EmitSoundToAll", N_EmitToAll },
            { "Source2ModSounds.StopSound", N_StopSound },
            { "Source2ModSounds.StopSoundForPlayers", N_StopSoundForPlayers },
            { "Source2ModSounds.StopSoundByName", N_StopSoundByName },
            { "Source2ModSounds.SetSoundVolume", N_SetSoundVolume },
            { "Source2ModSounds.UpdateSoundParams", N_UpdateSoundParams },
            { "Source2ModSounds.HookSound", N_HookSound },
            { "Source2ModSounds.UnhookSound", N_UnhookSound },
            { "Source2ModSounds.HashSoundName", N_HashSoundName },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetNetSoundNatives()
    {
        return g_netSoundNatives;
    }
}
