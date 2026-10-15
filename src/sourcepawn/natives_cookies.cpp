/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * SourcePawn natives for client preferences: Source2ModCookies
 * (source2mod/cookies.inc) and SourceMod's clientprefs.inc (Cookie,
 * RegClientCookie, Get/SetClientCookie, ...), both on ISource2ModCookies.
 * The cookie menus (SetCookiePrefabMenu, SetCookieMenuItem, ShowCookieMenu)
 * and the cookie iterator are not there yet.
 *
 * Project: Source2Mod
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/cookies.h"

#include <cstring>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        struct CookieObject : HandleObject
        {
            explicit CookieObject(Source2ModCookieId id) : id(id) {}
            Source2ModCookieId id;
        };

        HandleType CookieType()
        {
            static const HandleType s_type = Handles().CreateType("Cookie", [](void* p) { delete static_cast<HandleObject*>(p); });
            return s_type;
        }

        cookies::CookiesManager& Cookies()
        {
            return cookies::cookiesManager;
        }

        PluginId CallerId(IPluginContext* ctx)
        {
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            return plugin ? plugin->Id() : 0;
        }

        bool SlotOf(IPluginContext* ctx, cell_t client, CPlayerSlot& slot)
        {
            if (client < 1 || client > kMaxClients)
            {
                ctx->ThrowNativeError("Client index %d is invalid", client);
                return false;
            }
            slot = CPlayerSlot(client - 1);
            return true;
        }

        /// A CPlayerSlot argument (source2mod/*.inc pass slots, 0-based).
        bool PlayerSlot(IPluginContext* ctx, cell_t value, CPlayerSlot& slot)
        {
            if (value < 0 || value >= kMaxClients)
            {
                ctx->ThrowNativeError("Player slot %d is invalid", value);
                return false;
            }
            slot = CPlayerSlot(value);
            return true;
        }

        /// The cookie behind a Cookie handle; 0 and a script error for a bad one.
        Source2ModCookieId CookieOf(IPluginContext* ctx, cell_t handle)
        {
            auto* object = static_cast<CookieObject*>(ReadHandle(ctx, handle, CookieType()));
            return object ? object->id : 0;
        }

        /// A new Cookie handle for the caller; 0 for no cookie.
        cell_t NewCookieHandle(IPluginContext* ctx, Source2ModCookieId id)
        {
            if (!id)
                return 0;
            auto* object = new CookieObject(id);
            const cell_t handle = Handles().Create(CookieType(), object, CallerId(ctx));
            if (!handle)
            {
                delete object;
                return ctx->ThrowNativeError("Handle limit reached");
            }
            return handle;
        }

        Source2ModCookieAccess AccessOf(cell_t value)
        {
            return value == SOURCE2MOD_COOKIE_PROTECTED ? SOURCE2MOD_COOKIE_PROTECTED
                 : value == SOURCE2MOD_COOKIE_PRIVATE   ? SOURCE2MOD_COOKIE_PRIVATE
                                                        : SOURCE2MOD_COOKIE_PUBLIC;
        }

        cell_t SetValue(IPluginContext* ctx, CPlayerSlot slot, Source2ModCookieId cookie, const char* value)
        {
            if (!cookie)
                return 0;
            if (strlen(value) > SOURCE2MOD_COOKIE_MAX_VALUE_LENGTH)
                return ctx->ThrowNativeError("Cookie value is longer than %d bytes", SOURCE2MOD_COOKIE_MAX_VALUE_LENGTH);
            return Cookies().SetCookie(slot, cookie, value);
        }

        cell_t GetValue(IPluginContext* ctx, CPlayerSlot slot, Source2ModCookieId cookie, cell_t addr, cell_t maxlen)
        {
            if (!cookie)
                return 0;
            char value[SOURCE2MOD_COOKIE_MAX_VALUE_LENGTH + 1];
            const bool ok = Cookies().GetCookie(slot, cookie, value, sizeof(value));
            ctx->StringToLocalUTF8(addr, maxlen, ok ? value : "", nullptr);
            return ok;
        }

        cell_t SetByAuthId(IPluginContext* ctx, const char* authId, Source2ModCookieId cookie, const char* value)
        {
            if (!cookie)
                return 0;
            const uint64 steamId = cookies::CookiesManager::ParseAuthId(authId);
            if (!steamId)
                return ctx->ThrowNativeError("Invalid auth id \"%s\"", authId);
            return Cookies().SetCookieBySteamID(steamId, cookie, value);
        }

        cell_t ClientTime(CPlayerSlot slot, Source2ModCookieId cookie)
        {
            if (!cookie)
                return 0;
            return static_cast<cell_t>(Cookies().GetCookieTime(slot, cookie));
        }

        // ---- SourceMod clientprefs.inc --------------------------------------------

        // ---- Source2ModCookies ----------------------------------------------------

        cell_t N_S2MIsConnected(IPluginContext*, const cell_t*)
        {
            return Cookies().IsConnected();
        }

        // (const char[] name, const char[] description, CookieAccess access)
        cell_t N_S2MRegister(IPluginContext* ctx, const cell_t* params)
        {
            return Cookies().RegisterCookie(LocalString(ctx, params[1]), LocalString(ctx, params[2]), AccessOf(params[3]));
        }

        cell_t N_S2MFind(IPluginContext* ctx, const cell_t* params)
        {
            return Cookies().FindCookie(LocalString(ctx, params[1]));
        }

        cell_t N_S2MCount(IPluginContext*, const cell_t*)
        {
            return Cookies().GetCookieCount();
        }

        // (int cookie, char[] buffer, int maxlen)
        cell_t N_S2MGetName(IPluginContext* ctx, const cell_t* params)
        {
            const char* name = Cookies().GetCookieName(params[1]);
            ctx->StringToLocalUTF8(params[2], params[3], name ? name : "", nullptr);
            return name != nullptr;
        }

        cell_t N_S2MGetDescription(IPluginContext* ctx, const cell_t* params)
        {
            const char* description = Cookies().GetCookieDescription(params[1]);
            ctx->StringToLocalUTF8(params[2], params[3], description ? description : "", nullptr);
            return description != nullptr;
        }

        cell_t N_S2MGetAccess(IPluginContext*, const cell_t* params)
        {
            return Cookies().GetCookieAccess(params[1]);
        }

        // (CPlayerSlot slot)
        cell_t N_S2MAreCached(IPluginContext* ctx, const cell_t* params)
        {
            CPlayerSlot slot(0);
            return PlayerSlot(ctx, params[1], slot) && Cookies().AreCookiesCached(slot);
        }

        // (CPlayerSlot slot, int cookie, char[] buffer, int maxlen)
        cell_t N_S2MGet(IPluginContext* ctx, const cell_t* params)
        {
            CPlayerSlot slot(0);
            return PlayerSlot(ctx, params[1], slot) ? GetValue(ctx, slot, params[2], params[3], params[4]) : 0;
        }

        // (CPlayerSlot slot, int cookie, const char[] value)
        cell_t N_S2MSet(IPluginContext* ctx, const cell_t* params)
        {
            CPlayerSlot slot(0);
            return PlayerSlot(ctx, params[1], slot) ? SetValue(ctx, slot, params[2], LocalString(ctx, params[3])) : 0;
        }

        // (CPlayerSlot slot, int cookie)
        cell_t N_S2MGetTime(IPluginContext* ctx, const cell_t* params)
        {
            CPlayerSlot slot(0);
            return PlayerSlot(ctx, params[1], slot) ? ClientTime(slot, params[2]) : 0;
        }

        // (const char[] steamId, int cookie, const char[] value)
        cell_t N_S2MSetBySteamID(IPluginContext* ctx, const cell_t* params)
        {
            return SetByAuthId(ctx, LocalString(ctx, params[1]), params[2], LocalString(ctx, params[3]));
        }

        const sp_nativeinfo_t g_cookieNatives[] = {

            { "Source2ModCookies.IsConnected", N_S2MIsConnected },
            { "Source2ModCookies.Register", N_S2MRegister },
            { "Source2ModCookies.Find", N_S2MFind },
            { "Source2ModCookies.Count", N_S2MCount },
            { "Source2ModCookies.GetName", N_S2MGetName },
            { "Source2ModCookies.GetDescription", N_S2MGetDescription },
            { "Source2ModCookies.GetAccess", N_S2MGetAccess },
            { "Source2ModCookies.AreCached", N_S2MAreCached },
            { "Source2ModCookies.Get", N_S2MGet },
            { "Source2ModCookies.Set", N_S2MSet },
            { "Source2ModCookies.GetTime", N_S2MGetTime },
            { "Source2ModCookies.SetBySteamID", N_S2MSetBySteamID },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetCookieNatives()
    {
        return g_cookieNatives;
    }
}
