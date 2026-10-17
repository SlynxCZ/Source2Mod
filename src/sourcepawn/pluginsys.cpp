/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * Plugin to plugin: natives made by plugins (Source2ModNatives.CreateNative and
 * its Get.../Set... methods, ISource2ModNatives for native plugins),
 * libraries (Source2ModNatives.RegisterLibrary/LibraryExists, OnLibraryAdded/Removed) and
 * forwards (GlobalForward, PrivateForward, Source2ModCall). SourceMod's
 * smn_fakenatives.cpp, ForwardSys.cpp, smn_functions.cpp and the library
 * handling in PluginSys.cpp (AlliedModders LLC) are the reference for the
 * behaviour.
 *
 * A native a plugin makes is an INativeCallback kept by name for the life of
 * the core. Scripts bind to that object, so when its plugin unloads the
 * native only goes dead -- a call raises an error -- and when the plugin
 * comes back it lives again under the same binding.
 *
 * Project: Source2Mod
 */
#include "natives.h"
#include "crashinfo.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/hookid.h"
#include "core/pluginmanager.h"
#include "core/scheduler.h"
#include "utils/log.h"

#include <source2mod/ISource2ModNatives.h>

#include <base-runtime.h>
#include <objects.h>
#include <v2/runtime.h>
#include <sp_callable.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        ScriptPlugin* Caller(IPluginContext* ctx)
        {
            return ScriptPlugin::FromContext(ctx);
        }

        // ---- natives made by plugins ----------------------------------------------

        // The native call being served by a script (Source2ModNatives.GetCell & co. read it).
        // A C++ caller (ISource2ModScriptNative) has no context: `caller` is
        // null, `params` holds each argument's cell (0 for the ones passed by
        // address) and `host` the arguments themselves.
        struct NativeFrame
        {
            IPluginContext* caller;
            const cell_t* params;
            IPluginContext* owner;
            const Source2ModScriptArg* host = nullptr;
        };
        std::vector<NativeFrame> g_nativeFrames;

        // The cell a script reads with GetCell for a C++ argument.
        cell_t HostArgCell(const Source2ModScriptArg& arg)
        {
            switch (arg.type)
            {
            case Source2ModScriptArgType::Cell:
            case Source2ModScriptArgType::Bool:
                return arg.cell;
            case Source2ModScriptArgType::Float:
                return sp_ftoc(arg.flt);
            case Source2ModScriptArgType::Entity:
                return EntityCell(arg.entity);
            default:
                return 0;
            }
        }

        class DynamicNative final : public INativeCallback
        {
        public:
            explicit DynamicNative(std::string name) : m_name(std::move(name)) {}

            void AddRef() override { ++m_refs; }
            void Release() override
            {
                if (--m_refs == 0)
                    delete this;
            }

            cell_t Invoke(IPluginContext* ctx, const cell_t* params) override
            {
                if (!m_owner)
                    return ctx->ThrowNativeError("Native \"%s\" is not available (its plugin unloaded)", m_name.c_str());

                if (m_hostFn)
                    return m_hostFn(ctx, params);

                // NativeCall: any (Handle plugin, int numParams)
                ScriptPlugin* caller = Caller(ctx);
                sp::CallArgs args;
                args.PushCell(caller ? caller->Handle() : 0);
                args.PushCell(params[0]);

                g_nativeFrames.push_back({ ctx, params, m_scriptFn->GetParentRuntime()->GetDefaultContext() });
                cell_t result = 0;
                // No exception handler: an error in the native -- ThrowNativeError
                // included -- unwinds into the script that called it, as in SourceMod.
                {
                    crashinfo::CallScope call(m_scriptFn);
                    m_scriptFn->Invoke(args, &result);
                }
                g_nativeFrames.pop_back();
                return result;
            }

            // From C++: the script's NativeCall with `plugin` INVALID_HANDLE;
            // an error in it stops there (reported on the console).
            bool InvokeFromHost(const Source2ModScriptArg* hostArgs, int count, cell_t* result, std::string& error)
            {
                if (!m_owner)
                {
                    error = "native \"" + m_name + "\" is not available (no plugin serves it)";
                    return false;
                }
                if (!m_scriptFn)
                {
                    error = "native \"" + m_name + "\" belongs to a native plugin; call its code directly";
                    return false;
                }
                if (count < 0 || (count > 0 && !hostArgs) || static_cast<size_t>(count) >= SP_MAX_EXEC_PARAMS)
                {
                    error = "bad argument list";
                    return false;
                }

                std::vector<cell_t> params(static_cast<size_t>(count) + 1);
                params[0] = count;
                for (int i = 0; i < count; i++)
                    params[i + 1] = HostArgCell(hostArgs[i]);

                sp::CallArgs args;
                args.PushCell(0);
                args.PushCell(count);

                IPluginFunction* fn = m_scriptFn;
                g_nativeFrames.push_back({ nullptr, params.data(), fn->GetParentRuntime()->GetDefaultContext(), hostArgs });
                cell_t res = 0;
                const bool ok = sourcepawn::Invoke(fn, args, &res);
                g_nativeFrames.pop_back();

                if (result)
                    *result = ok ? res : 0;
                if (!ok)
                    error = "native \"" + m_name + "\" raised an error (see the server console)";
                return ok;
            }

            const std::string& Name() const { return m_name; }
            PluginId Owner() const { return m_owner; }
            bool Alive() const { return m_owner != 0; }
            bool IsScript() const { return m_scriptFn != nullptr; }

            void SetScript(PluginId owner, IPluginFunction* fn)
            {
                m_owner = owner;
                m_scriptFn = fn;
                m_hostFn = nullptr;
            }

            void SetHost(PluginId owner, SPVM_NATIVE_FUNC fn)
            {
                m_owner = owner;
                m_scriptFn = nullptr;
                m_hostFn = fn;
            }

            void Kill()
            {
                m_owner = 0;
                m_scriptFn = nullptr;
                m_hostFn = nullptr;
            }

        private:
            std::string m_name;
            int m_refs = 1;
            PluginId m_owner = 0;
            IPluginFunction* m_scriptFn = nullptr;
            SPVM_NATIVE_FUNC m_hostFn = nullptr;
        };

        // One reference each, held for the life of the core.
        std::unordered_map<std::string, DynamicNative*> g_dynamicNatives;

        // Scripts that did not load for want of a native, by path.
        std::set<std::string> g_waiting;
        bool g_retryQueued = false;

        void RetryWaiting()
        {
            if (g_retryQueued || g_waiting.empty())
                return;
            g_retryQueued = true;

            // Next frame: the plugin that registered the natives is still in
            // the middle of its own load.
            scheduler::schedulerManager.NextFrame(0, []()
            {
                g_retryQueued = false;
                const std::set<std::string> paths = std::move(g_waiting);
                g_waiting.clear();
                for (const std::string& path : paths)
                {
                    if (pluginManager.IsPluginLoaded(path))
                        continue;
                    char err[256]{};
                    pluginManager.LoadPluginFromPath(path.c_str(), err, sizeof(err), false);
                }
            });
        }

        // New natives reach the scripts already running, and the ones waiting.
        void NativesAdded()
        {
            for (ScriptPlugin* script : ScriptPlugin::Running())
                script->BindDynamicNatives();
            RetryWaiting();
        }

        DynamicNative* NativeSlot(const std::string& name, PluginId owner, std::string& error)
        {
            auto it = g_dynamicNatives.find(name);
            if (it == g_dynamicNatives.end())
                it = g_dynamicNatives.emplace(name, new DynamicNative(name)).first;
            else if (it->second->Alive() && it->second->Owner() != owner)
            {
                error = "Native \"" + name + "\" was already registered by another plugin";
                return nullptr;
            }
            return it->second;
        }

        bool AddHostNative(PluginId owner, const char* name, SPVM_NATIVE_FUNC fn)
        {
            std::string error;
            DynamicNative* native = NativeSlot(name, owner, error);
            if (!native)
            {
                FP_ERROR("AddNatives: {}", error);
                return false;
            }
            native->SetHost(owner, fn);
            return true;
        }

        NativeFrame* CurrentNative(IPluginContext* ctx)
        {
            if (g_nativeFrames.empty() || g_nativeFrames.back().owner != ctx)
            {
                ctx->ThrowNativeError("Not called from inside a native function");
                return nullptr;
            }
            return &g_nativeFrames.back();
        }

        NativeFrame* NativeParam(IPluginContext* ctx, cell_t param)
        {
            NativeFrame* frame = CurrentNative(ctx);
            if (!frame)
                return nullptr;
            if (param < 1 || param > frame->params[0])
            {
                ctx->ThrowNativeError("Invalid parameter number: %d", param);
                return nullptr;
            }
            return frame;
        }

        // A C++ caller's argument `param` (checked by NativeParam).
        const Source2ModScriptArg& HostArg(const NativeFrame* frame, cell_t param)
        {
            return frame->host[param - 1];
        }

        cell_t WrongHostArg(IPluginContext* ctx, cell_t param, const char* wanted)
        {
            return ctx->ThrowNativeError("Parameter %d from the C++ caller is not %s", param, wanted);
        }

        // Source2ModNatives.CreateNative(const char[] name, NativeCall func)
        cell_t N_CreateNative(IPluginContext* ctx, const cell_t* params)
        {
            ScriptPlugin* plugin = Caller(ctx);
            IPluginFunction* fn = ctx->GetFunctionById(params[2]);
            if (!plugin || !fn)
                return ctx->ThrowNativeError("Invalid function id (%X)", params[2]);

            const char* name = LocalString(ctx, params[1]);
            if (!*name)
                return ctx->ThrowNativeError("Invalid native name");

            std::string error;
            DynamicNative* native = NativeSlot(name, plugin->Id(), error);
            if (!native)
                return ctx->ThrowNativeError("%s", error.c_str());

            native->SetScript(plugin->Id(), fn);
            NativesAdded();
            return 0;
        }

        // Source2ModNatives.ThrowError(int error, const char[] fmt, any ...)
        cell_t N_ThrowNativeError(IPluginContext* ctx, const cell_t* params)
        {
            if (!CurrentNative(ctx))
                return 0;
            char msg[1024];
            if (FormatParams(ctx, params, 2, msg, sizeof(msg)))
                ctx->ReportError("%s", msg);
            return 0;
        }

        // Source2ModNatives.GetStringLength(int param, int &length)
        cell_t N_GetNativeStringLength(IPluginContext* ctx, const cell_t* params)
        {
            NativeFrame* frame = NativeParam(ctx, params[1]);
            if (!frame)
                return 0;
            const char* str = nullptr;
            if (frame->host)
            {
                const Source2ModScriptArg& arg = HostArg(frame, params[1]);
                if (arg.type != Source2ModScriptArgType::String && arg.type != Source2ModScriptArgType::StringBuffer)
                    return WrongHostArg(ctx, params[1], "a string");
                str = arg.AsString();
            }
            else
            {
                char* local = nullptr;
                const int err = frame->caller->LocalToString(frame->params[params[1]], &local);
                if (err != SP_ERROR_NONE)
                    return err;
                str = local;
            }
            cell_t* out;
            ctx->LocalToPhysAddr(params[2], &out);
            *out = static_cast<cell_t>(strlen(str));
            return SP_ERROR_NONE;
        }

        // Source2ModNatives.GetString(int param, char[] buffer, int maxlength, int &bytes=0)
        cell_t N_GetNativeString(IPluginContext* ctx, const cell_t* params)
        {
            NativeFrame* frame = NativeParam(ctx, params[1]);
            if (!frame)
                return 0;
            const char* str = nullptr;
            if (frame->host)
            {
                const Source2ModScriptArg& arg = HostArg(frame, params[1]);
                if (arg.type != Source2ModScriptArgType::String && arg.type != Source2ModScriptArgType::StringBuffer)
                    return WrongHostArg(ctx, params[1], "a string");
                str = arg.AsString();
            }
            else
            {
                char* local = nullptr;
                const int err = frame->caller->LocalToString(frame->params[params[1]], &local);
                if (err != SP_ERROR_NONE)
                    return err;
                str = local;
            }
            size_t written = 0;
            ctx->StringToLocalUTF8(params[2], params[3], str, &written);
            cell_t* bytes;
            if (params[0] >= 4 && ctx->LocalToPhysAddr(params[4], &bytes) == SP_ERROR_NONE)
                *bytes = static_cast<cell_t>(written);
            return SP_ERROR_NONE;
        }

        // Source2ModNatives.SetString(int param, const char[] source, int maxlength, bool utf8=true, int &bytes=0)
        cell_t N_SetNativeString(IPluginContext* ctx, const cell_t* params)
        {
            NativeFrame* frame = NativeParam(ctx, params[1]);
            if (!frame)
                return 0;
            const char* source = LocalString(ctx, params[2]);
            size_t written = 0;
            int err = SP_ERROR_NONE;
            if (frame->host)
            {
                const Source2ModScriptArg& arg = HostArg(frame, params[1]);
                if (arg.type != Source2ModScriptArgType::StringBuffer || !arg.buffer || arg.size == 0)
                    return WrongHostArg(ctx, params[1], "a string buffer");
                const size_t room = std::min<size_t>(arg.size, params[3] > 0 ? static_cast<size_t>(params[3]) : 0);
                if (room > 0)
                {
                    // UTF-8 safe like StringToLocalUTF8: no sequence cut in half.
                    written = std::min(strlen(source), room - 1);
                    while (written > 0 && written < strlen(source) && (static_cast<unsigned char>(source[written]) & 0xC0) == 0x80)
                        written--;
                    memcpy(arg.buffer, source, written);
                    arg.buffer[written] = '\0';
                }
            }
            else if (params[0] < 4 || params[4])
                err = frame->caller->StringToLocalUTF8(frame->params[params[1]], params[3], source, &written);
            else
            {
                err = frame->caller->StringToLocal(frame->params[params[1]], params[3], source);
                written = std::min(strlen(source), params[3] > 0 ? static_cast<size_t>(params[3] - 1) : 0);
            }
            if (err != SP_ERROR_NONE)
                return err;
            cell_t* bytes;
            if (params[0] >= 5 && ctx->LocalToPhysAddr(params[5], &bytes) == SP_ERROR_NONE)
                *bytes = static_cast<cell_t>(written);
            return SP_ERROR_NONE;
        }

        cell_t N_GetNativeCell(IPluginContext* ctx, const cell_t* params)
        {
            NativeFrame* frame = NativeParam(ctx, params[1]);
            return frame ? frame->params[params[1]] : 0;
        }

        // A Function is only an id; it means the same in the caller's plugin.
        // A C++ caller has no functions to pass: INVALID_FUNCTION.
        cell_t N_GetNativeFunction(IPluginContext* ctx, const cell_t* params)
        {
            NativeFrame* frame = NativeParam(ctx, params[1]);
            return frame && !frame->host ? frame->params[params[1]] : 0;
        }

        cell_t N_GetNativeCellRef(IPluginContext* ctx, const cell_t* params)
        {
            NativeFrame* frame = NativeParam(ctx, params[1]);
            if (!frame)
                return 0;
            if (frame->host)
            {
                const Source2ModScriptArg& arg = HostArg(frame, params[1]);
                if (arg.type == Source2ModScriptArgType::CellRef && arg.cellRef)
                    return *arg.cellRef;
                if (arg.type == Source2ModScriptArgType::FloatRef && arg.floatRef)
                    return sp_ftoc(*arg.floatRef);
                return WrongHostArg(ctx, params[1], "a reference");
            }
            cell_t* addr;
            if (const int err = frame->caller->LocalToPhysAddr(frame->params[params[1]], &addr); err != SP_ERROR_NONE)
                return ctx->ThrowNativeErrorEx(err, "Could not read argument");
            return *addr;
        }

        cell_t N_SetNativeCellRef(IPluginContext* ctx, const cell_t* params)
        {
            NativeFrame* frame = NativeParam(ctx, params[1]);
            if (!frame)
                return 0;
            if (frame->host)
            {
                const Source2ModScriptArg& arg = HostArg(frame, params[1]);
                if (arg.type == Source2ModScriptArgType::CellRef && arg.cellRef)
                    *arg.cellRef = params[2];
                else if (arg.type == Source2ModScriptArgType::FloatRef && arg.floatRef)
                    *arg.floatRef = sp_ctof(params[2]);
                else
                    return WrongHostArg(ctx, params[1], "a reference");
                return 0;
            }
            cell_t* addr;
            if (const int err = frame->caller->LocalToPhysAddr(frame->params[params[1]], &addr); err != SP_ERROR_NONE)
                return ctx->ThrowNativeErrorEx(err, "Could not read argument");
            *addr = params[2];
            return 0;
        }

        // Source2ModNatives.GetArray(int param, any[] local, int size)
        cell_t N_GetNativeArray(IPluginContext* ctx, const cell_t* params)
        {
            NativeFrame* frame = NativeParam(ctx, params[1]);
            if (!frame)
                return 0;
            cell_t* src;
            cell_t* dest;
            size_t count = static_cast<size_t>(std::max(params[3], 0));
            if (frame->host)
            {
                const Source2ModScriptArg& arg = HostArg(frame, params[1]);
                if (arg.type != Source2ModScriptArgType::Array || !arg.array)
                    return WrongHostArg(ctx, params[1], "an array");
                src = arg.array;
                count = std::min<size_t>(count, arg.size);
            }
            else if (int err = frame->caller->LocalToPhysAddr(frame->params[params[1]], &src); err != SP_ERROR_NONE)
                return err;
            ctx->LocalToPhysAddr(params[2], &dest);
            memcpy(dest, src, count * sizeof(cell_t));
            return SP_ERROR_NONE;
        }

        // Source2ModNatives.SetArray(int param, const any[] local, int size)
        cell_t N_SetNativeArray(IPluginContext* ctx, const cell_t* params)
        {
            NativeFrame* frame = NativeParam(ctx, params[1]);
            if (!frame)
                return 0;
            cell_t* src;
            cell_t* dest;
            size_t count = static_cast<size_t>(std::max(params[3], 0));
            if (frame->host)
            {
                // Only an array the C++ caller passed with copyback takes writes.
                const Source2ModScriptArg& arg = HostArg(frame, params[1]);
                if (arg.type != Source2ModScriptArgType::Array || !arg.array || !arg.copyback)
                    return WrongHostArg(ctx, params[1], "an array passed with copyback");
                dest = arg.array;
                count = std::min<size_t>(count, arg.size);
            }
            else if (int err = frame->caller->LocalToPhysAddr(frame->params[params[1]], &dest); err != SP_ERROR_NONE)
                return err;
            ctx->LocalToPhysAddr(params[2], &src);
            memcpy(dest, src, count * sizeof(cell_t));
            return SP_ERROR_NONE;
        }

        cell_t N_IsNativeParamNullVector(IPluginContext* ctx, const cell_t* params)
        {
            NativeFrame* frame = NativeParam(ctx, params[1]);
            if (!frame)
                return 0;
            if (frame->host)
                return HostArg(frame, params[1]).type == Source2ModScriptArgType::NullVector;
            cell_t* addr = nullptr;
            frame->caller->LocalToPhysAddr(frame->params[params[1]], &addr);
            return addr == frame->caller->GetNullRef(SP_NULL_VECTOR);
        }

        cell_t N_IsNativeParamNullString(IPluginContext* ctx, const cell_t* params)
        {
            NativeFrame* frame = NativeParam(ctx, params[1]);
            if (!frame)
                return 0;
            if (frame->host)
                return HostArg(frame, params[1]).type == Source2ModScriptArgType::NullString;
            char* str = nullptr;
            frame->caller->LocalToStringNULL(frame->params[params[1]], &str);
            return str == nullptr;
        }

        // Source2ModNatives.FormatString(int out_param, int fmt_param, int vararg_param, int out_len,
        //                               int &written=0, char[] out_string="", const char[] fmt_string="")
        // The format and its arguments are the caller's (fmt_param, with the
        // arguments right after it); the result goes to the caller's out_param
        // or, with out_param 0, to out_string here.
        cell_t N_FormatNativeString(IPluginContext* ctx, const cell_t* params)
        {
            NativeFrame* frame = CurrentNative(ctx);
            if (!frame)
                return 0;
            if (frame->host)
                return ctx->ThrowNativeError("Source2ModNatives.FormatString: the native was called from C++, there is no format to read");

            const cell_t outParam = params[1];
            const cell_t fmtParam = params[2];
            const cell_t varParam = params[3];
            const cell_t outLen = params[4];
            if (fmtParam < 1 || fmtParam > frame->params[0])
                return ctx->ThrowNativeError("Source2ModNatives.FormatString: a format from fmt_string is not supported, pass fmt_param");
            if (varParam != 0 && varParam != fmtParam + 1)
                return ctx->ThrowNativeError("Source2ModNatives.FormatString: the arguments must follow the format (vararg_param %d)", varParam);
            if (outLen < 1)
                return ctx->ThrowNativeError("Invalid output length: %d", outLen);

            std::string buf(static_cast<size_t>(outLen), '\0');
            size_t written = 0;
            if (!FormatParams(frame->caller, frame->params, fmtParam, buf.data(), buf.size(), &written))
                return SP_ERROR_PARAM;

            if (outParam)
            {
                if (outParam > frame->params[0])
                    return ctx->ThrowNativeError("Invalid parameter number: %d", outParam);
                frame->caller->StringToLocalUTF8(frame->params[outParam], outLen, buf.c_str(), &written);
            }
            else if (params[0] >= 6)
                ctx->StringToLocalUTF8(params[6], outLen, buf.c_str(), &written);

            cell_t* out;
            if (params[0] >= 5 && ctx->LocalToPhysAddr(params[5], &out) == SP_ERROR_NONE)
                *out = static_cast<cell_t>(written);
            return SP_ERROR_NONE;
        }

        // ---- libraries --------------------------------------------------------

        struct Library
        {
            std::vector<PluginId> owners;
        };
        std::map<std::string, Library> g_libraries;

        // Registered during a load, announced once it is done.
        std::map<PluginId, std::vector<std::string>> g_unannounced;

        // C++ plugins' AddLibraryListener().
        hookid::HookList<Source2ModLibraryHandler> g_libraryListeners;

        void Announce(const std::string& name, bool added)
        {
            for (ScriptPlugin* script : ScriptPlugin::Running())
                script->CallForwardString(added ? "OnLibraryAdded" : "OnLibraryRemoved", name.c_str());
            for (const Source2ModLibraryHandler& handler : g_libraryListeners.Snapshot())
                handler(name.c_str(), added);
        }

        // A library's first owner announces it.
        bool AddLibrary(PluginId owner, const std::string& name, bool announceNow)
        {
            Library& lib = g_libraries[name];
            if (std::find(lib.owners.begin(), lib.owners.end(), owner) != lib.owners.end())
                return true;
            lib.owners.push_back(owner);
            if (lib.owners.size() > 1)
                return true;

            if (announceNow)
                Announce(name, true);
            else
                g_unannounced[owner].push_back(name);
            return true;
        }

        cell_t N_RegPluginLibrary(IPluginContext* ctx, const cell_t* params)
        {
            ScriptPlugin* plugin = Caller(ctx);
            if (!plugin)
                return 0;
            AddLibrary(plugin->Id(), LocalString(ctx, params[1]), plugin->IsRunning());
            return 0;
        }

        cell_t N_LibraryExists(IPluginContext* ctx, const cell_t* params)
        {
            return pluginsys::LibraryAvailable(LocalString(ctx, params[1]));
        }

        // ---- forwards ---------------------------------------------------------

        // A null string / vector argument. SourcePawn's CallArgs no longer has
        // one (upstream c6358600); a script tells NULL_STRING / NULL_VECTOR
        // from a real argument by its address (IsNullString), so the argument
        // is that function's own pubvar, passed as its address. 0 for a script
        // without it.
        cell_t NullArgAddress(IPluginFunction* fn, const char* pubvar)
        {
            IPluginRuntime* runtime = fn ? fn->GetParentRuntime() : nullptr;
            uint32_t index = 0;
            cell_t local = 0;
            cell_t* phys = nullptr;
            if (!runtime || runtime->FindPubvarByName(pubvar, &index) != SP_ERROR_NONE
                || runtime->GetPubvarAddrs(index, &local, &phys) != SP_ERROR_NONE)
                return 0;
            return local;
        }

        // Keeps the slices NullStringArg made alive until the call is over.
        using NullSlices = std::vector<sp::Handle<sp::SpArray>>;

        // A null string argument for fn. A SourcePawn 2 function takes a
        // string parameter as an array reference, not an address: a slice
        // over its NULL_STRING, which IsNullString then recognises. A
        // SourcePawn 1 function takes the address itself.
        cell_t NullStringArg(IPluginFunction* fn, NullSlices& keep)
        {
            const cell_t local = NullArgAddress(fn, "NULL_STRING");
            auto* runtime = fn ? static_cast<sp::BaseRuntime*>(fn->GetParentRuntime()) : nullptr;
            sp::v2::Runtime* v2 = runtime ? runtime->AsV2() : nullptr;
            if (!v2 || !local)
                return local;
            sp::Handle<sp::SpArray> slice = v2->NewSliceEs(static_cast<uint32_t>(local), 1);
            if (!slice)
                return local;
            const cell_t ref = static_cast<cell_t>(v2->heap().ToLocalAddr(slice.get()));
            keep.push_back(std::move(slice));
            return ref;
        }

        // ExecType
        constexpr cell_t ET_Ignore = 0;
        constexpr cell_t ET_Single = 1;
        constexpr cell_t ET_Event = 2;
        constexpr cell_t ET_Hook = 3;
        constexpr cell_t ET_LowEvent = 4;

        // ParamType (functions.inc) as far as a C++ hook tells them apart.
        constexpr cell_t kParamAny = 0;
        constexpr cell_t kParamFloat = 2 << 1;
        constexpr cell_t kParamFloatByRef = (2 << 1) | 1;

        constexpr size_t kMaxCallParams = 32;
        // INVALID_FUNCTION: the compiler makes it null, 0; no function has that id.
        constexpr cell_t kInvalidFunction = 0;

        struct Forward;
        std::set<Forward*> g_forwardObjects;

        struct Forward : HandleObject
        {
            struct Fn
            {
                PluginId owner;
                IPluginFunction* fn;
            };

            std::string name;    // a global forward's public; empty for a private one
            cell_t execType = ET_Ignore;
            std::vector<cell_t> paramTypes;
            std::vector<Fn> functions;

            Forward() { g_forwardObjects.insert(this); }
            ~Forward() override { g_forwardObjects.erase(this); }

            bool Global() const { return !name.empty(); }

            // A global forward is every running script's public by that name,
            // in load order; a private one is its list.
            std::vector<IPluginFunction*> Targets() const
            {
                std::vector<IPluginFunction*> out;
                if (Global())
                {
                    for (ScriptPlugin* script : ScriptPlugin::Running())
                        if (IPluginFunction* fn = script->Runtime()->GetFunctionByName(name.c_str()))
                            out.push_back(fn);
                }
                else
                {
                    for (const Fn& f : functions)
                        out.push_back(f.fn);
                }
                return out;
            }
        };

        // A forward's result over its functions, by ExecType.
        struct ResultCombiner
        {
            explicit ResultCombiner(cell_t type)
                : execType(type), result(type == ET_LowEvent ? kActionSupersede : kActionIgnore)
            {
            }

            // false: ET_Hook got Action_Supersede, the rest are not called.
            bool Add(cell_t res)
            {
                if (execType == ET_Single || !any)
                    result = (execType == ET_Ignore) ? 0 : res;
                else if (execType == ET_Event || execType == ET_Hook)
                    result = std::max(result, res);
                else if (execType == ET_LowEvent)
                    result = std::min(result, res);
                any = true;
                return !(execType == ET_Hook && res >= kActionSupersede);
            }

            cell_t Result() const { return execType == ET_Ignore ? 0 : result; }

            cell_t execType;
            cell_t result;
            bool any = false;
        };

        // C++ HookScriptForward(), by forward name.
        std::map<std::string, hookid::HookList<Source2ModScriptForwardHandler>> g_forwardHooks;

        // Runs the C++ hooks of a global forward; false when one stopped it.
        bool RunForwardHooks(const std::string& name, Source2ModScriptArg* args, int count, ResultCombiner& combiner)
        {
            auto it = g_forwardHooks.find(name);
            if (it == g_forwardHooks.end())
                return true;
            for (const Source2ModScriptForwardHandler& handler : it->second.Snapshot())
            {
                if (!combiner.Add(handler(name.c_str(), args, count)))
                    return false;
            }
            return true;
        }

        size_t ForwardHookCount(const std::string& name)
        {
            auto it = g_forwardHooks.find(name);
            return it == g_forwardHooks.end() ? 0 : it->second.size();
        }

        // The ExecType of a script's GlobalForward by this name; ET_Event without one.
        cell_t GlobalExecType(const std::string& name)
        {
            for (const Forward* fwd : g_forwardObjects)
                if (fwd->Global() && fwd->name == name)
                    return fwd->execType;
            return ET_Event;
        }

        Forward* ForwardOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<Forward*>(ReadHandle(ctx, handle, Types().forward));
        }

        Forward* PrivateForwardOf(IPluginContext* ctx, cell_t handle)
        {
            Forward* fwd = ForwardOf(ctx, handle);
            if (fwd && fwd->Global())
            {
                ctx->ThrowNativeError("Handle %x is a global forward; functions are added to private forwards only", handle);
                return nullptr;
            }
            return fwd;
        }

        // The `ParamType ...` of a forward's constructor: by reference, from `first` on.
        cell_t MakeForward(IPluginContext* ctx, const cell_t* params, std::string name, cell_t execType, int first)
        {
            if (static_cast<size_t>(std::max(params[0] - first + 1, 0)) > kMaxCallParams)
                return ctx->ThrowNativeError("Too many parameters for a forward (max %d)", static_cast<int>(kMaxCallParams));

            auto* fwd = new Forward();
            fwd->name = std::move(name);
            fwd->execType = execType;
            for (int i = first; i <= params[0]; i++)
            {
                cell_t* type;
                ctx->LocalToPhysAddr(params[i], &type);
                fwd->paramTypes.push_back(*type);
            }

            ScriptPlugin* plugin = Caller(ctx);
            const cell_t handle = Handles().Create(Types().forward, fwd, plugin ? plugin->Id() : 0);
            if (!handle)
            {
                delete fwd;
                return ctx->ThrowNativeError("Handle limit reached");
            }
            return handle;
        }

        // GlobalForward(const char[] name, ExecType type, ParamType ...)
        cell_t N_CreateGlobalForward(IPluginContext* ctx, const cell_t* params)
        {
            const char* name = LocalString(ctx, params[1]);
            if (!*name)
                return ctx->ThrowNativeError("A global forward needs a name");
            return MakeForward(ctx, params, name, params[2], 3);
        }

        // PrivateForward(ExecType type, ParamType ...)
        cell_t N_CreateForward(IPluginContext* ctx, const cell_t* params)
        {
            return MakeForward(ctx, params, "", params[1], 2);
        }

        cell_t N_GetForwardFunctionCount(IPluginContext* ctx, const cell_t* params)
        {
            Forward* fwd = ForwardOf(ctx, params[1]);
            return fwd ? static_cast<cell_t>(fwd->Targets().size()) : 0;
        }

        // PrivateForward.AddFunction(Handle plugin, Function func)
        cell_t N_AddToForward(IPluginContext* ctx, const cell_t* params)
        {
            Forward* fwd = PrivateForwardOf(ctx, params[1]);
            ScriptPlugin* plugin = fwd ? ScriptPlugin::FromHandle(ctx, params[2]) : nullptr;
            if (!plugin)
                return 0;
            IPluginFunction* fn = plugin->Runtime()->GetFunctionById(static_cast<funcid_t>(params[3]));
            if (!fn)
                return ctx->ThrowNativeError("Invalid function id (%X)", params[3]);

            for (const Forward::Fn& f : fwd->functions)
                if (f.fn == fn)
                    return 0;
            fwd->functions.push_back({ plugin->Id(), fn });
            return 1;
        }

        // PrivateForward.RemoveFunction(Handle plugin, Function func)
        cell_t N_RemoveFromForward(IPluginContext* ctx, const cell_t* params)
        {
            Forward* fwd = PrivateForwardOf(ctx, params[1]);
            ScriptPlugin* plugin = fwd ? ScriptPlugin::FromHandle(ctx, params[2]) : nullptr;
            if (!plugin)
                return 0;
            IPluginFunction* fn = plugin->Runtime()->GetFunctionById(static_cast<funcid_t>(params[3]));
            return std::erase_if(fwd->functions, [fn](const Forward::Fn& f) { return f.fn == fn; }) > 0;
        }

        // PrivateForward.RemoveAllFunctions(Handle plugin)
        cell_t N_RemoveAllFromForward(IPluginContext* ctx, const cell_t* params)
        {
            Forward* fwd = PrivateForwardOf(ctx, params[1]);
            ScriptPlugin* plugin = fwd ? ScriptPlugin::FromHandle(ctx, params[2]) : nullptr;
            if (!plugin)
                return 0;
            const PluginId id = plugin->Id();
            return static_cast<cell_t>(std::erase_if(fwd->functions, [id](const Forward::Fn& f) { return f.owner == id; }));
        }

        // Source2ModCall.GetFunctionByName(Handle plugin, const char[] name)
        cell_t N_GetFunctionByName(IPluginContext* ctx, const cell_t* params)
        {
            ScriptPlugin* plugin = ScriptPlugin::FromHandle(ctx, params[1]);
            if (!plugin)
                return kInvalidFunction;
            IPluginFunction* fn = plugin->Runtime()->GetFunctionByName(LocalString(ctx, params[2]));
            return fn ? static_cast<cell_t>(fn->GetFunctionID()) : kInvalidFunction;
        }

        // ---- Call_* -------------------------------------------------------------

        struct CallParam
        {
            // NullVector / NullString: the callee's own NULL_VECTOR / NULL_STRING,
            // so its IsNullVector / IsNullString see them.
            enum Kind { Cell, Ref, Array, String, NullVector, NullString } kind = Cell;
            cell_t value = 0;
            cell_t callerAddr = 0;          // where a copyback goes
            bool copyback = false;
            std::vector<cell_t> cells;
            std::vector<char> chars;
            int stringFlags = 0;
        };

        struct CallState
        {
            bool active = false;
            IPluginContext* ctx = nullptr;
            Forward* fwd = nullptr;
            cell_t fwdHandle = 0;
            IPluginFunction* fn = nullptr;
            std::vector<CallParam> params;
        };
        CallState g_call;

        bool CheckCanStart(IPluginContext* ctx)
        {
            if (g_call.active)
            {
                ctx->ThrowNativeError("Cannot call another function or forward while a call is in progress");
                return false;
            }
            return true;
        }

        CallParam* NewParam(IPluginContext* ctx)
        {
            if (!g_call.active)
            {
                ctx->ThrowNativeError("Cannot push parameters when there is no call in progress");
                return nullptr;
            }
            if (g_call.params.size() >= kMaxCallParams)
            {
                ctx->ThrowNativeError("Cannot push more than %d parameters", static_cast<int>(kMaxCallParams));
                return nullptr;
            }
            return &g_call.params.emplace_back();
        }

        cell_t N_Call_StartForward(IPluginContext* ctx, const cell_t* params)
        {
            if (!CheckCanStart(ctx))
                return 0;
            Forward* fwd = ForwardOf(ctx, params[1]);
            if (!fwd)
                return 0;
            g_call = CallState{};
            g_call.active = true;
            g_call.ctx = ctx;
            g_call.fwd = fwd;
            g_call.fwdHandle = params[1];
            return 0;
        }

        // Source2ModCall.StartFunction(Handle plugin, Function func)
        cell_t N_Call_StartFunction(IPluginContext* ctx, const cell_t* params)
        {
            if (!CheckCanStart(ctx))
                return 0;
            ScriptPlugin* plugin = ScriptPlugin::FromHandle(ctx, params[1]);
            if (!plugin)
                return 0;
            IPluginFunction* fn = plugin->Runtime()->GetFunctionById(static_cast<funcid_t>(params[2]));
            if (!fn)
                return ctx->ThrowNativeError("Invalid function id (%X)", params[2]);
            g_call = CallState{};
            g_call.active = true;
            g_call.ctx = ctx;
            g_call.fn = fn;
            return 0;
        }

        cell_t N_Call_PushCell(IPluginContext* ctx, const cell_t* params)
        {
            if (CallParam* p = NewParam(ctx))
                p->value = params[1];
            return 0;
        }

        cell_t N_Call_PushCellRef(IPluginContext* ctx, const cell_t* params)
        {
            CallParam* p = NewParam(ctx);
            if (!p)
                return 0;
            cell_t* addr;
            ctx->LocalToPhysAddr(params[1], &addr);
            p->kind = CallParam::Ref;
            p->cells.assign(1, *addr);
            p->callerAddr = params[1];
            p->copyback = true;
            return 0;
        }

        // Source2ModCall.PushArray(const any[] value, int size) / Source2ModCall.PushArrayEx(any[] value, int size, int cpflags)
        cell_t PushArray(IPluginContext* ctx, const cell_t* params, bool copyback)
        {
            CallParam* p = NewParam(ctx);
            if (!p)
                return 0;
            if (params[2] < 0)
                return ctx->ThrowNativeError("Invalid array size: %d", params[2]);
            cell_t* addr;
            ctx->LocalToPhysAddr(params[1], &addr);
            p->kind = CallParam::Array;
            p->cells.assign(addr, addr + params[2]);
            p->callerAddr = params[1];
            p->copyback = copyback;
            return 0;
        }

        cell_t N_Call_PushArray(IPluginContext* ctx, const cell_t* params) { return PushArray(ctx, params, false); }
        cell_t N_Call_PushArrayEx(IPluginContext* ctx, const cell_t* params) { return PushArray(ctx, params, (params[3] & SM_PARAM_COPYBACK) != 0); }

        cell_t N_Call_PushNullVector(IPluginContext* ctx, const cell_t*)
        {
            if (CallParam* p = NewParam(ctx))
                p->kind = CallParam::NullVector;
            return 0;
        }

        cell_t N_Call_PushString(IPluginContext* ctx, const cell_t* params)
        {
            CallParam* p = NewParam(ctx);
            if (!p)
                return 0;
            const char* str = LocalString(ctx, params[1]);
            p->kind = CallParam::String;
            p->chars.assign(str, str + strlen(str) + 1);
            p->stringFlags = SM_PARAM_STRING_COPY | SM_PARAM_STRING_UTF8;
            return 0;
        }

        // Source2ModCall.PushStringEx(char[] value, int length, int szflags, int cpflags). The
        // script's SM_PARAM_STRING_* are the C++ ones shifted down a bit.
        cell_t N_Call_PushStringEx(IPluginContext* ctx, const cell_t* params)
        {
            CallParam* p = NewParam(ctx);
            if (!p)
                return 0;
            if (params[2] < 1)
                return ctx->ThrowNativeError("Invalid string length: %d", params[2]);
            char* str;
            ctx->LocalToString(params[1], &str);
            p->kind = CallParam::String;
            p->chars.assign(static_cast<size_t>(params[2]), '\0');
            memcpy(p->chars.data(), str, std::min(strlen(str) + 1, p->chars.size()));
            p->chars.back() = '\0';
            p->stringFlags = params[3] << 1;
            p->callerAddr = params[1];
            p->copyback = (params[4] & SM_PARAM_COPYBACK) != 0;
            return 0;
        }

        cell_t N_Call_PushNullString(IPluginContext* ctx, const cell_t*)
        {
            if (CallParam* p = NewParam(ctx))
                p->kind = CallParam::NullString;
            return 0;
        }

        cell_t N_Call_Cancel(IPluginContext* ctx, const cell_t*)
        {
            if (!g_call.active)
                return ctx->ThrowNativeError("No call is in progress");
            g_call = CallState{};
            return 0;
        }

        // Source2ModCall.Finish(any &result=0): the error code, SP_ERROR_NONE when every call went through.
        cell_t N_Call_Finish(IPluginContext* ctx, const cell_t* params)
        {
            if (!g_call.active)
                return ctx->ThrowNativeError("No call is in progress");

            // Taken out first: a function in the forward may start a call of its own.
            CallState call = std::move(g_call);
            g_call = CallState{};

            std::vector<IPluginFunction*> targets;
            cell_t execType = ET_Single;
            if (call.fwd)
            {
                // The forward may have been closed by now.
                if (!Handles().Read(call.fwdHandle, Types().forward))
                    return ctx->ThrowNativeError("Invalid Handle %x (error %d)", call.fwdHandle, static_cast<int>(HandleError::Freed));
                targets = call.fwd->Targets();
                execType = call.fwd->execType;
            }
            else
                targets.push_back(call.fn);

            ResultCombiner combiner(execType);
            bool go = true;
            int error = SP_ERROR_NONE;
            for (IPluginFunction* fn : targets)
            {
                NullSlices nullSlices;
                sp::CallArgs args;
                for (CallParam& p : call.params)
                {
                    switch (p.kind)
                    {
                    case CallParam::Cell:
                        args.PushCell(p.value);
                        break;
                    case CallParam::Ref:
                        args.PushCellByRef(p.cells.data(), SM_PARAM_COPYBACK);
                        break;
                    case CallParam::Array:
                        args.PushArray(p.cells.data(), static_cast<unsigned>(p.cells.size()), SM_PARAM_COPYBACK);
                        break;
                    case CallParam::String:
                        args.PushString(p.chars.data(), p.chars.size(), p.stringFlags | (p.copyback ? SM_PARAM_COPYBACK : 0));
                        break;
                    case CallParam::NullVector:
                        args.PushCell(NullArgAddress(fn, "NULL_VECTOR"));
                        break;
                    case CallParam::NullString:
                        args.PushCell(NullStringArg(fn, nullSlices));
                        break;
                    }
                }

                cell_t res = 0;
                if (!Invoke(fn, args, &res))
                {
                    error = SP_ERROR_ABORTED;
                    continue;
                }

                if (!(go = combiner.Add(res)))
                    break;
            }

            // A global forward reaches C++ hooks too, over the same buffers.
            if (go && call.fwd && call.fwd->Global() && ForwardHookCount(call.fwd->name))
            {
                std::vector<Source2ModScriptArg> hostArgs;
                for (size_t i = 0; i < call.params.size(); i++)
                {
                    CallParam& p = call.params[i];
                    const cell_t type = i < call.fwd->paramTypes.size() ? call.fwd->paramTypes[i] : kParamAny;
                    switch (p.kind)
                    {
                    case CallParam::Cell:
                        hostArgs.push_back(type == kParamFloat ? Source2ModScriptArg::Float(sp_ctof(p.value)) : Source2ModScriptArg::Int(p.value));
                        break;
                    case CallParam::Ref:
                        hostArgs.push_back(type == kParamFloatByRef ? Source2ModScriptArg::FloatRef(reinterpret_cast<float*>(p.cells.data()))
                                                                    : Source2ModScriptArg::CellRef(p.cells.data()));
                        break;
                    case CallParam::Array:
                        hostArgs.push_back(Source2ModScriptArg::Array(p.cells.data(), p.cells.size(), true));
                        break;
                    case CallParam::String:
                        hostArgs.push_back(p.copyback ? Source2ModScriptArg::Buffer(p.chars.data(), p.chars.size())
                                                      : Source2ModScriptArg::String(p.chars.data()));
                        break;
                    case CallParam::NullVector:
                        hostArgs.push_back(Source2ModScriptArg::Null(Source2ModScriptArgType::NullVector));
                        break;
                    case CallParam::NullString:
                        hostArgs.push_back(Source2ModScriptArg::Null(Source2ModScriptArgType::NullString));
                        break;
                    }
                }
                RunForwardHooks(call.fwd->name, hostArgs.data(), static_cast<int>(hostArgs.size()), combiner);
            }
            const cell_t result = combiner.Result();

            // What the functions changed goes back to the caller.
            for (CallParam& p : call.params)
            {
                if (!p.copyback || !p.callerAddr)
                    continue;
                if (p.kind == CallParam::String)
                {
                    char* dest;
                    if (ctx->LocalToString(p.callerAddr, &dest) == SP_ERROR_NONE)
                        memcpy(dest, p.chars.data(), p.chars.size());
                }
                else
                {
                    cell_t* dest;
                    if (ctx->LocalToPhysAddr(p.callerAddr, &dest) == SP_ERROR_NONE)
                        memcpy(dest, p.cells.data(), p.cells.size() * sizeof(cell_t));
                }
            }

            cell_t* out;
            if (params[0] >= 1 && ctx->LocalToPhysAddr(params[1], &out) == SP_ERROR_NONE)
                *out = result;
            return error;
        }

        const sp_nativeinfo_t g_pluginSysNatives[] = {
            // source2mod/functions.inc: Source2ModNatives (C++ ISource2ModNatives)
            { "Source2ModNatives.CreateNative", N_CreateNative },
            { "Source2ModNatives.ThrowError", N_ThrowNativeError },
            { "Source2ModNatives.GetStringLength", N_GetNativeStringLength },
            { "Source2ModNatives.GetString", N_GetNativeString },
            { "Source2ModNatives.SetString", N_SetNativeString },
            { "Source2ModNatives.GetCell", N_GetNativeCell },
            { "Source2ModNatives.GetFunction", N_GetNativeFunction },
            { "Source2ModNatives.GetCellRef", N_GetNativeCellRef },
            { "Source2ModNatives.SetCellRef", N_SetNativeCellRef },
            { "Source2ModNatives.GetArray", N_GetNativeArray },
            { "Source2ModNatives.SetArray", N_SetNativeArray },
            { "Source2ModNatives.IsParamNullVector", N_IsNativeParamNullVector },
            { "Source2ModNatives.IsParamNullString", N_IsNativeParamNullString },
            { "Source2ModNatives.FormatString", N_FormatNativeString },
            { "Source2ModNatives.RegisterLibrary", N_RegPluginLibrary },
            { "Source2ModNatives.LibraryExists", N_LibraryExists },
            // GlobalForward / PrivateForward
            { "GlobalForward.GlobalForward", N_CreateGlobalForward },
            { "GlobalForward.FunctionCount.get", N_GetForwardFunctionCount },
            { "PrivateForward.PrivateForward", N_CreateForward },
            { "PrivateForward.AddFunction", N_AddToForward },
            { "PrivateForward.RemoveFunction", N_RemoveFromForward },
            { "PrivateForward.RemoveAllFunctions", N_RemoveAllFromForward },
            // Source2ModCall
            { "Source2ModCall.GetFunctionByName", N_GetFunctionByName },
            { "Source2ModCall.StartForward", N_Call_StartForward },
            { "Source2ModCall.StartFunction", N_Call_StartFunction },
            { "Source2ModCall.PushCell", N_Call_PushCell },
            { "Source2ModCall.PushCellRef", N_Call_PushCellRef },
            { "Source2ModCall.PushFloat", N_Call_PushCell },
            { "Source2ModCall.PushFloatRef", N_Call_PushCellRef },
            { "Source2ModCall.PushArray", N_Call_PushArray },
            { "Source2ModCall.PushArrayEx", N_Call_PushArrayEx },
            { "Source2ModCall.PushNullVector", N_Call_PushNullVector },
            { "Source2ModCall.PushString", N_Call_PushString },
            { "Source2ModCall.PushStringEx", N_Call_PushStringEx },
            { "Source2ModCall.PushNullString", N_Call_PushNullString },
            { "Source2ModCall.Finish", N_Call_Finish },
            { "Source2ModCall.Cancel", N_Call_Cancel },
            { nullptr, nullptr },
        };

        // ---- scripts from C++ (ISource2ModNatives002) ---------------------------

        // A plugin-made native by name, looked up at each call: it outlives
        // the script, a call after it unloaded is refused.
        class ScriptNativeRef final : public ISource2ModScriptNative
        {
        public:
            explicit ScriptNativeRef(std::string name) : m_name(std::move(name)) {}

            const char* GetName() const override { return m_name.c_str(); }

            bool IsAvailable() const override
            {
                const DynamicNative* native = Slot();
                return native && native->Alive() && native->IsScript();
            }

            PluginId GetOwner() const override
            {
                const DynamicNative* native = Slot();
                return native ? native->Owner() : 0;
            }

            bool Invoke(const Source2ModScriptArg* args, int count, cell_t* result) override
            {
                m_error.clear();
                if (result)
                    *result = 0;
                DynamicNative* native = Slot();
                if (!native)
                {
                    m_error = "native \"" + m_name + "\" was never registered";
                    return false;
                }
                return native->InvokeFromHost(args, count, result, m_error);
            }

            const char* GetLastError() const override { return m_error.c_str(); }

            CEntityInstance* EntityFromCell(cell_t value) const override { return sourcepawn::EntityFromCell(value); }

        private:
            DynamicNative* Slot() const
            {
                auto it = g_dynamicNatives.find(m_name);
                return it == g_dynamicNatives.end() ? nullptr : it->second;
            }

            std::string m_name;
            std::string m_error;
        };

        // A global forward by name: the running scripts' publics, then the C++ hooks.
        class ScriptForwardRef final : public ISource2ModScriptForward
        {
        public:
            explicit ScriptForwardRef(std::string name) : m_name(std::move(name)) {}

            const char* GetName() const override { return m_name.c_str(); }

            bool IsAvailable() const override { return GetFunctionCount() > 0; }

            int GetFunctionCount() const override
            {
                size_t count = ForwardHookCount(m_name);
                for (ScriptPlugin* script : ScriptPlugin::Running())
                    if (script->Runtime()->GetFunctionByName(m_name.c_str()))
                        count++;
                return static_cast<int>(count);
            }

            bool Invoke(const Source2ModScriptArg* args, int count, cell_t* result) override
            {
                m_error.clear();
                if (result)
                    *result = 0;
                if (count < 0 || (count > 0 && !args) || static_cast<size_t>(count) > kMaxCallParams)
                {
                    m_error = "bad argument list";
                    return false;
                }

                // The hooks get a copy that points at the same buffers, with
                // entities as the script cells a script firing it would pass.
                std::vector<Source2ModScriptArg> hostArgs(args, args + count);
                sp::CallArgs callArgs;
                // Argument index -> pubvar, for the null arguments (NullArgAddress).
                std::vector<std::pair<uint32_t, const char*>> nulls;
                for (Source2ModScriptArg& arg : hostArgs)
                {
                    switch (arg.type)
                    {
                    case Source2ModScriptArgType::Cell:
                    case Source2ModScriptArgType::Bool:
                        callArgs.PushCell(arg.cell);
                        break;
                    case Source2ModScriptArgType::Float:
                        callArgs.PushFloat(arg.flt);
                        break;
                    case Source2ModScriptArgType::Entity:
                        arg = Source2ModScriptArg::Int(EntityCell(arg.entity));
                        callArgs.PushCell(arg.cell);
                        break;
                    case Source2ModScriptArgType::String:
                        callArgs.PushString(arg.string ? arg.string : "");
                        break;
                    case Source2ModScriptArgType::StringBuffer:
                        if (!arg.buffer || arg.size == 0)
                        {
                            m_error = "a string buffer argument without room";
                            return false;
                        }
                        callArgs.PushString(arg.buffer, arg.size, SM_PARAM_STRING_COPY | SM_PARAM_STRING_UTF8 | SM_PARAM_COPYBACK);
                        break;
                    case Source2ModScriptArgType::Array:
                        callArgs.PushArray(arg.array, arg.size, arg.copyback ? SM_PARAM_COPYBACK : 0);
                        break;
                    case Source2ModScriptArgType::CellRef:
                        callArgs.PushCellByRef(arg.cellRef);
                        break;
                    case Source2ModScriptArgType::FloatRef:
                        callArgs.PushFloatByRef(arg.floatRef);
                        break;
                    case Source2ModScriptArgType::NullString:
                        nulls.emplace_back(callArgs.argc, "NULL_STRING");
                        callArgs.PushCell(0);
                        break;
                    case Source2ModScriptArgType::NullVector:
                        nulls.emplace_back(callArgs.argc, "NULL_VECTOR");
                        callArgs.PushCell(0);
                        break;
                    }
                }

                ResultCombiner combiner(GlobalExecType(m_name));
                bool go = true;
                bool failed = false;
                for (ScriptPlugin* script : ScriptPlugin::Running())
                {
                    IPluginFunction* fn = script->Runtime()->GetFunctionByName(m_name.c_str());
                    if (!fn)
                        continue;
                    NullSlices nullSlices;
                    for (const auto& [index, pubvar] : nulls)
                        callArgs.argv[index].u.value = !strcmp(pubvar, "NULL_STRING")
                            ? NullStringArg(fn, nullSlices) : NullArgAddress(fn, pubvar);
                    cell_t res = 0;
                    if (!sourcepawn::Invoke(fn, callArgs, &res))
                    {
                        failed = true;
                        continue;
                    }
                    if (!(go = combiner.Add(res)))
                        break;
                }
                if (go)
                    RunForwardHooks(m_name, hostArgs.data(), count, combiner);

                if (result)
                    *result = combiner.Result();
                if (failed)
                    m_error = "a script function of forward \"" + m_name + "\" raised an error (see the server console)";
                return !failed;
            }

            const char* GetLastError() const override { return m_error.c_str(); }

            CEntityInstance* EntityFromCell(cell_t value) const override { return sourcepawn::EntityFromCell(value); }

        private:
            std::string m_name;
            std::string m_error;
        };

        // For the life of the core: plugins keep the pointers.
        std::unordered_map<std::string, std::unique_ptr<ScriptNativeRef>> g_scriptNativeRefs;
        std::unordered_map<std::string, std::unique_ptr<ScriptForwardRef>> g_scriptForwardRefs;

        // ---- ISource2ModNatives ---------------------------------------------------

        class NativesApi final : public ISource2ModNatives
        {
        public:
            bool AddNatives(PluginId owner, const sp_nativeinfo_t* natives) override
            {
                if (!natives)
                    return false;
                bool all = true;
                for (const sp_nativeinfo_t* n = natives; n->name; ++n)
                    all &= n->func && AddHostNative(owner, n->name, n->func);
                NativesAdded();
                return all;
            }

            bool RegisterLibrary(PluginId owner, const char* name) override
            {
                if (!name || !*name)
                    return false;
                return AddLibrary(owner, name, true);
            }

            bool LibraryExists(const char* name) override
            {
                return pluginsys::LibraryAvailable(name);
            }

            PluginId GetScriptPlugin(IPluginContext* ctx) override
            {
                ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
                return plugin ? plugin->Id() : 0;
            }

            bool FormatString(IPluginContext* ctx, const cell_t* params, int fmtParam, char* buffer, size_t maxlen, size_t* written) override
            {
                return FormatParams(ctx, params, fmtParam, buffer, maxlen, written);
            }

            Source2ModHandleType CreateHandleType(PluginId owner, const char* name, Source2ModHandleDestructor destroy) override
            {
                std::function<void(void*)> fn;
                if (destroy)
                    fn = [destroy](void* object) { destroy(object); };
                return Handles().CreateType(name, std::move(fn), 0, true, owner);
            }

            cell_t CreateHandle(Source2ModHandleType type, void* object, IPluginContext* ctx) override
            {
                return Handles().Create(type, object, GetScriptPlugin(ctx));
            }

            void* ReadHandle(IPluginContext* ctx, cell_t handle, Source2ModHandleType type) override
            {
                return sourcepawn::ReadHandle(ctx, handle, type);
            }

            bool CloseHandle(cell_t handle) override
            {
                return Handles().Free(handle, 0, true) == HandleError::None;
            }

            ISource2ModScriptNative* FindScriptNative(const char* name) override
            {
                if (!name || !*name)
                    return nullptr;
                auto& ref = g_scriptNativeRefs[name];
                if (!ref)
                    ref = std::make_unique<ScriptNativeRef>(name);
                return ref.get();
            }

            ISource2ModScriptForward* FindScriptForward(const char* name) override
            {
                if (!name || !*name)
                    return nullptr;
                auto& ref = g_scriptForwardRefs[name];
                if (!ref)
                    ref = std::make_unique<ScriptForwardRef>(name);
                return ref.get();
            }

            Source2ModHookId HookScriptForward(const char* name, Source2ModScriptForwardHandler handler) override
            {
                if (!name || !*name || !handler)
                    return 0;
                const PluginId owner = hookid::OwnerOfHandler(handler);
                return g_forwardHooks[name].Add(owner, std::move(handler));
            }

            bool UnhookScriptForward(Source2ModHookId id) override
            {
                for (auto& [name, hooks] : g_forwardHooks)
                    if (hooks.RemoveId(id))
                        return true;
                return false;
            }

            Source2ModHookId AddLibraryListener(Source2ModLibraryHandler handler, bool replay) override
            {
                if (!handler)
                    return 0;
                const PluginId owner = hookid::OwnerOfHandler(handler);
                const Source2ModLibraryHandler first = handler;
                const Source2ModHookId id = g_libraryListeners.Add(owner, std::move(handler));
                if (replay)
                {
                    std::vector<std::string> names;
                    for (const auto& [name, lib] : g_libraries)
                        if (!lib.owners.empty())
                            names.push_back(name);
                    for (const std::string& name : names)
                        first(name.c_str(), true);
                }
                return id;
            }

            bool RemoveLibraryListener(Source2ModHookId id) override
            {
                return g_libraryListeners.RemoveId(id);
            }

            CEntityInstance* EntityFromScriptCell(cell_t value) override
            {
                return sourcepawn::EntityFromCell(value);
            }

            cell_t ScriptCellFromEntity(CEntityInstance* entity) override
            {
                return EntityCell(entity);
            }
        };

        NativesApi g_nativesApi;
    }

    const sp_nativeinfo_t* GetPluginSysNatives()
    {
        return g_pluginSysNatives;
    }

    ISource2ModNatives* NativesInterface()
    {
        return &g_nativesApi;
    }

    namespace pluginsys
    {
        int NativeStatus(const char* name)
        {
            auto it = g_dynamicNatives.find(name ? name : "");
            if (it == g_dynamicNatives.end())
                return -1;
            return it->second->Alive() ? 1 : 0;
        }

        bool LibraryAvailable(const char* name)
        {
            if (!name)
                return false;
            // SourceMod's CanTestFeatures(): GetFeatureStatus is there.
            if (!strcmp(name, "__CanTestFeatures__"))
                return true;
            auto it = g_libraries.find(name);
            return it != g_libraries.end() && !it->second.owners.empty();
        }

        bool BindNative(sp::BaseRuntime* runtime, uint32_t index, const char* name)
        {
            auto it = g_dynamicNatives.find(name);
            if (it == g_dynamicNatives.end() || !it->second->Alive())
                return false;
            const sp_native_t* native = runtime->GetNative(index);
            const uint32_t flags = (native ? native->flags : 0) | SP_NTVFLAG_EPHEMERAL;
            return runtime->UpdateNativeBindingObject(index, it->second, flags, nullptr) == SP_ERROR_NONE;
        }

        void NoteWaiting(const std::string& path)
        {
            g_waiting.insert(path);
        }

        void OnPluginLoaded(PluginId id)
        {
            // Its own libraries, to everyone running (itself included)...
            if (auto it = g_unannounced.find(id); it != g_unannounced.end())
            {
                const std::vector<std::string> names = std::move(it->second);
                g_unannounced.erase(it);
                for (const std::string& name : names)
                    Announce(name, true);
            }

            // ...and, as SourceMod does, the libraries already there to it.
            for (ScriptPlugin* script : ScriptPlugin::Running())
            {
                if (script->Id() != id)
                    continue;
                for (const auto& [name, lib] : g_libraries)
                {
                    const bool own = std::find(lib.owners.begin(), lib.owners.end(), id) != lib.owners.end();
                    if (!own && !lib.owners.empty())
                        script->CallForwardString("OnLibraryAdded", name.c_str());
                }
            }
        }

        void RemoveAllForPlugin(PluginId id)
        {
            // Its own C++ listeners first: it hears nothing of its own going.
            g_libraryListeners.RemoveOwner(id);
            for (auto& [name, hooks] : g_forwardHooks)
                hooks.RemoveOwner(id);

            for (auto& [name, native] : g_dynamicNatives)
                if (native->Owner() == id)
                    native->Kill();

            // What was never announced added is not announced removed either.
            std::vector<std::string> unannounced;
            if (auto u = g_unannounced.find(id); u != g_unannounced.end())
            {
                unannounced = std::move(u->second);
                g_unannounced.erase(u);
            }
            for (auto it = g_libraries.begin(); it != g_libraries.end();)
            {
                std::erase(it->second.owners, id);
                if (it->second.owners.empty())
                {
                    const std::string name = it->first;
                    it = g_libraries.erase(it);
                    if (std::find(unannounced.begin(), unannounced.end(), name) == unannounced.end())
                        Announce(name, false);
                }
                else
                    ++it;
            }

            for (Forward* fwd : g_forwardObjects)
                std::erase_if(fwd->functions, [id](const Forward::Fn& f) { return f.owner == id; });

            if (g_call.active && (g_call.fn || g_call.fwd))
            {
                if (ScriptPlugin* caller = ScriptPlugin::FromContext(g_call.ctx); !caller || caller->Id() == id)
                    g_call = CallState{};
            }
        }
    }
}
