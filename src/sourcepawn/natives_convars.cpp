/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * ConVar natives (convars.inc, AutoExecConfig from source2mod.inc) on the
 * core's ConVars manager. SourceMod's smn_console.cpp / ConVarManager
 * (AlliedModders LLC) are the reference: one handle per ConVar, owned by the
 * core and never closed, the same handle from CreateConVar and FindConVar,
 * change hooks per plugin and callback.
 *
 * CS2 ConVars are typed where Source 1's were strings. A script's ConVar is
 * made an int when its default and bounds are whole numbers, a float when
 * they are numbers, a string otherwise; the getters and setters convert
 * either way.
 *
 * Project: Source2Mod
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/convars.h"
#include "utils/log.h"

#include <sp_callable.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        // Through the interface, whose overloads have the slot default.
        ISource2ModConVars& CV()
        {
            return convars::convarsManager;
        }

        std::unordered_map<uint16, cell_t> g_conVarHandles;

        // ConVar.AddChangeHook / HookConVarChange: by plugin, ConVar and callback.
        std::map<std::tuple<PluginId, uint16, funcid_t>, Source2ModHookId> g_changeHooks;

        // A ConVar handle holds its access index + 1, so it is never null.
        uint16 IndexOf(IPluginContext* ctx, cell_t handle, bool* ok)
        {
            void* object = ReadHandle(ctx, handle, Types().conVar);
            *ok = object != nullptr;
            return object ? static_cast<uint16>(reinterpret_cast<uintptr_t>(object) - 1) : SOURCE2MOD_INVALID_CONVAR_INDEX;
        }

#define CONVAR_OR_RETURN(idx, handle, ret)          \
        bool idx##_ok;                              \
        const uint16 idx = IndexOf(ctx, handle, &idx##_ok); \
        if (!idx##_ok)                              \
            return ret

        bool IsWholeNumber(const char* s)
        {
            if (!*s)
                return false;
            char* end;
            strtol(s, &end, 10);
            return *end == '\0';
        }

        bool IsNumber(const char* s)
        {
            if (!*s)
                return false;
            char* end;
            strtof(s, &end);
            return *end == '\0';
        }

        bool IsWhole(float v)
        {
            return std::floor(v) == v;
        }

        cell_t N_GetBool(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            // A string ConVar reads as a number, as Source 1's did.
            if (CV().GetType(idx) == EConVarType_String)
                return atoi(CV().GetString(idx)) != 0;
            return CV().GetBool(idx);
        }

        cell_t N_GetInt(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            const EConVarType type = CV().GetType(idx);
            if (type == EConVarType_String)
                return atoi(CV().GetString(idx));
            if (type == EConVarType_Float32 || type == EConVarType_Float64)
                return static_cast<cell_t>(CV().GetFloat(idx));
            return CV().GetInt(idx);
        }

        cell_t N_GetFloat(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            const EConVarType type = CV().GetType(idx);
            if (type == EConVarType_String)
                return sp_ftoc(static_cast<float>(atof(CV().GetString(idx))));
            if (type == EConVarType_Float32 || type == EConVarType_Float64)
                return sp_ftoc(CV().GetFloat(idx));
            return sp_ftoc(static_cast<float>(CV().GetInt(idx)));
        }

        void SetValueString(uint16 idx, const char* value)
        {
            CV().SetString(idx, value);
        }

        // A number into whatever type the ConVar is; a string ConVar gets
        // the text Source 1 would have stored.
        void SetNumber(uint16 idx, double value, const char* text)
        {
            switch (CV().GetType(idx))
            {
            case EConVarType_Float32:
                CV().SetFloat(idx, static_cast<float>(value));
                break;
            case EConVarType_Float64:
                CV().SetDouble(idx, value);
                break;
            case EConVarType_Bool:
                CV().SetBool(idx, value != 0.0);
                break;
            case EConVarType_Int16:
            case EConVarType_UInt16:
            case EConVarType_Int32:
            case EConVarType_UInt32:
            case EConVarType_Int64:
            case EConVarType_UInt64:
                CV().SetInt(idx, static_cast<int32>(value));
                break;
            default:
                CV().SetString(idx, text);
                break;
            }
        }

        cell_t N_SetBool(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            SetNumber(idx, params[2] ? 1.0 : 0.0, params[2] ? "1" : "0");
            return 0;
        }

        cell_t N_SetInt(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            SetNumber(idx, params[2], std::to_string(params[2]).c_str());
            return 0;
        }

        cell_t N_SetFloat(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            char buf[64];
            snprintf(buf, sizeof(buf), "%f", static_cast<double>(sp_ctof(params[2])));
            SetNumber(idx, sp_ctof(params[2]), buf);
            return 0;
        }

        cell_t N_SetString(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            SetValueString(idx, LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_GetString(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            ctx->StringToLocalUTF8(params[2], params[3], CV().GetString(idx), nullptr);
            return 0;
        }

        std::string DefaultOf(uint16 idx)
        {
            ConVarRefAbstract ref(idx);
            if (!ref.IsValidRef() || !ref.IsConVarDataAvailable())
                return {};
            CBufferString buf;
            ref.GetDefaultAsString(buf);
            return buf.Get();
        }

        cell_t N_RestoreDefault(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            SetValueString(idx, DefaultOf(idx).c_str());
            return 0;
        }

        cell_t N_GetDefault(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            size_t written = 0;
            ctx->StringToLocalUTF8(params[2], params[3], DefaultOf(idx).c_str(), &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_GetFlags(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            return static_cast<cell_t>(CV().GetFlags(idx) & 0xFFFFFFFFu);
        }

        // The upper 32 flag bits are CS2's own; a script only sees and sets the lower.
        cell_t N_SetFlags(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            const uint64 high = CV().GetFlags(idx) & ~0xFFFFFFFFull;
            CV().SetFlags(idx, high | static_cast<uint32_t>(params[2]));
            return 0;
        }

        // GetBounds(ConVarBounds type, float &value): ConVarBound_Upper = 0, ConVarBound_Lower.
        cell_t N_GetBounds(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            ConVarRefAbstract ref(idx);
            if (!ref.IsValidRef() || !ref.IsConVarDataAvailable())
                return 0;

            const bool upper = params[2] == 0;
            if (upper ? !ref.HasMax() : !ref.HasMin())
                return 0;

            CBufferString buf;
            if (upper)
                ref.GetMaxAsString(buf);
            else
                ref.GetMinAsString(buf);

            cell_t* out;
            ctx->LocalToPhysAddr(params[3], &out);
            *out = sp_ftoc(static_cast<float>(atof(buf.Get())));
            return 1;
        }

        cell_t N_GetName(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            ctx->StringToLocalUTF8(params[2], params[3], CV().GetName(idx), nullptr);
            return 0;
        }

        cell_t N_GetDescription(IPluginContext* ctx, const cell_t* params)
        {
            CONVAR_OR_RETURN(idx, params[1], 0);
            const char* help = CV().GetHelpText(idx);
            ctx->StringToLocalUTF8(params[2], params[3], help ? help : "", nullptr);
            return 0;
        }

        // void (ConVar convar, const char[] oldValue, const char[] newValue)
        cell_t HookChange(IPluginContext* ctx, cell_t handle, cell_t funcId)
        {
            CONVAR_OR_RETURN(idx, handle, 0);
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            IPluginFunction* fn = ctx->GetFunctionById(funcId);
            if (!plugin || !fn)
                return ctx->ThrowNativeError("Invalid function id (%X)", funcId);

            const auto key = std::make_tuple(plugin->Id(), idx, static_cast<funcid_t>(funcId));
            if (g_changeHooks.count(key))
                return 0;

            ConVarChangeHandler handler([idx, handle, fn](ConVarRefAbstract* ref, CSplitScreenSlot, const char* newValue, const char* oldValue)
            {
                if (!ref || ref->GetAccessIndex() != idx)
                    return;
                // The engine reports every change, also a set to the same value.
                if (newValue && oldValue && strcmp(newValue, oldValue) == 0)
                    return;

                sp::CallArgs args;
                args.PushCell(handle);
                args.PushString(oldValue ? oldValue : "");
                args.PushString(newValue ? newValue : "");
                Invoke(fn, args);
            });

            g_changeHooks[key] = convars::convarsManager.HookConVarChange(plugin->Id(), std::move(handler));
            return 0;
        }

        cell_t UnhookChange(IPluginContext* ctx, cell_t handle, cell_t funcId)
        {
            CONVAR_OR_RETURN(idx, handle, 0);
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            auto it = g_changeHooks.find(std::make_tuple(plugin ? plugin->Id() : 0, idx, static_cast<funcid_t>(funcId)));
            if (it == g_changeHooks.end())
                return ctx->ThrowNativeError("No active hook on convar \"%s\" with callback %x", CV().GetName(idx), funcId);
            convars::convarsManager.UnhookConVarChangeId(it->second);
            g_changeHooks.erase(it);
            return 0;
        }

        cell_t N_AddChangeHook(IPluginContext* ctx, const cell_t* params) { return HookChange(ctx, params[1], params[2]); }
        cell_t N_RemoveChangeHook(IPluginContext* ctx, const cell_t* params) { return UnhookChange(ctx, params[1], params[2]); }

        // AutoExecConfig(bool autoCreate=true, const char[] name="", const char[] folder="source2mod")
        cell_t N_AutoExecConfig(IPluginContext* ctx, const cell_t* params)
        {
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            if (!plugin)
                return 0;
            std::string folder = params[0] >= 3 ? LocalString(ctx, params[3]) : "source2mod";
            // Scripts written for SourceMod pass its folder; theirs is ours.
            if (folder == "sourcemod")
                folder = "source2mod";
            plugin->RequestAutoExecConfig(params[0] < 1 || params[1], params[0] >= 2 ? LocalString(ctx, params[2]) : "", folder);
            return 0;
        }

        const sp_nativeinfo_t g_conVarNatives[] = {
            { "Source2ModConVars.AutoExecConfig", N_AutoExecConfig },
            { "ConVar.BoolValue.get", N_GetBool },
            { "ConVar.BoolValue.set", N_SetBool },
            { "ConVar.IntValue.get", N_GetInt },
            { "ConVar.IntValue.set", N_SetInt },
            { "ConVar.FloatValue.get", N_GetFloat },
            { "ConVar.FloatValue.set", N_SetFloat },
            { "ConVar.Flags.get", N_GetFlags },
            { "ConVar.Flags.set", N_SetFlags },
            { "ConVar.SetBool", N_SetBool },
            { "ConVar.SetInt", N_SetInt },
            { "ConVar.SetFloat", N_SetFloat },
            { "ConVar.GetString", N_GetString },
            { "ConVar.SetString", N_SetString },
            { "ConVar.RestoreDefault", N_RestoreDefault },
            { "ConVar.GetDefault", N_GetDefault },
            { "ConVar.GetBounds", N_GetBounds },
            { "ConVar.GetName", N_GetName },
            { "ConVar.GetDescription", N_GetDescription },
            { "ConVar.AddChangeHook", N_AddChangeHook },
            { "ConVar.RemoveChangeHook", N_RemoveChangeHook },
            { nullptr, nullptr },
        };
    }

    cell_t ConVarHandle(uint16_t accessIndex)
    {
        auto it = g_conVarHandles.find(accessIndex);
        if (it != g_conVarHandles.end() && Handles().Read(it->second, Types().conVar))
            return it->second;

        const cell_t handle = Handles().Create(Types().conVar, reinterpret_cast<void*>(static_cast<uintptr_t>(accessIndex) + 1), 0);
        g_conVarHandles[accessIndex] = handle;
        return handle;
    }

    void ForgetConVarHooks(PluginId id)
    {
        // The hooks themselves went with convarsManager.RemoveAllForPlugin().
        std::erase_if(g_changeHooks, [id](const auto& entry) { return std::get<0>(entry.first) == id; });
    }

    const sp_nativeinfo_t* GetConVarNatives()
    {
        return g_conVarNatives;
    }
}
