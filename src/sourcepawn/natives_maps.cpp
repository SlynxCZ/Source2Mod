/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * SourcePawn natives for maps: Source2ModMaps (source2mod/maps.inc) and
 * SourceMod's own names for the same (GetCurrentMap, IsMapValid, FindMap,
 * GetMapDisplayName -- halflife.inc; SetNextMap, GetNextMap, ForceChangeLevel,
 * GetMapHistory* -- nextmap.inc; ReadMapList -- source2mod.inc), all on
 * ISource2ModMaps.
 *
 * Project: Source2Mod
 */
#include "natives.h"
#include "scriptplugin.h"

#include "core/maps.h"

#include <functional>
#include <string>
#include <vector>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        // ByteCountToCells(PLATFORM_MAX_PATH): what a map name list holds per item.
        constexpr cell_t kMapBlockSize = 64;

        maps::MapsManager& Maps()
        {
            return maps::mapsManager;
        }

        PluginId CallerId(IPluginContext* ctx)
        {
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            return plugin ? plugin->Id() : 0;
        }

        cell_t ToBuffer(IPluginContext* ctx, cell_t addr, cell_t maxlen, const char* value)
        {
            size_t written = 0;
            ctx->StringToLocalUTF8(addr, maxlen, value ? value : "", &written);
            return static_cast<cell_t>(written);
        }

        // ---- shared by both spellings ------------------------------------------

        // (char[] buffer, int maxlen)
        cell_t N_GetCurrentMap(IPluginContext* ctx, const cell_t* params)
        {
            return ToBuffer(ctx, params[1], params[2], Maps().GetCurrentMap());
        }

        // (const char[] map)
        cell_t N_IsMapValid(IPluginContext* ctx, const cell_t* params)
        {
            return Maps().IsMapValid(LocalString(ctx, params[1]));
        }

        // (const char[] map, char[] found, int maxlen)
        cell_t N_FindMap(IPluginContext* ctx, const cell_t* params)
        {
            std::string found;
            const Source2ModFindMapResult result = Maps().FindMap(LocalString(ctx, params[1]), found);
            ToBuffer(ctx, params[2], params[3], found.c_str());
            return result;
        }

        // (const char[] map, char[] display, int maxlen)
        cell_t N_GetMapDisplayName(IPluginContext* ctx, const cell_t* params)
        {
            std::string found;
            if (Maps().FindMap(LocalString(ctx, params[1]), found) == SOURCE2MOD_FINDMAP_NOT_FOUND)
            {
                ToBuffer(ctx, params[2], params[3], "");
                return 0;
            }
            // "workshop/123/name" -> "name"
            if (found.rfind("workshop/", 0) == 0)
            {
                const size_t slash = found.rfind('/');
                if (slash > 8)
                    found = found.substr(slash + 1);
            }
            ToBuffer(ctx, params[2], params[3], found.c_str());
            return 1;
        }

        // (const char[] map)
        cell_t N_SetNextMap(IPluginContext* ctx, const cell_t* params)
        {
            return Maps().SetNextMap(LocalString(ctx, params[1]));
        }

        // (char[] map, int maxlen)
        cell_t N_GetNextMap(IPluginContext* ctx, const cell_t* params)
        {
            const char* next = Maps().GetNextMap();
            ToBuffer(ctx, params[1], params[2], next);
            return *next != '\0';
        }

        // (const char[] map, const char[] reason)
        cell_t N_ChangeLevel(IPluginContext* ctx, const cell_t* params)
        {
            const char* reason = params[0] >= 2 ? LocalString(ctx, params[2]) : "";
            return Maps().ChangeLevel(LocalString(ctx, params[1]), reason);
        }

        cell_t N_GetMapHistorySize(IPluginContext*, const cell_t*)
        {
            return Maps().GetMapHistorySize();
        }

        // (int item, char[] map, int mapLen, char[] reason, int reasonLen, int &startTime)
        bool MapHistory(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModMapHistoryEntry entry;
            if (!Maps().GetMapHistory(params[1], entry))
                return false;
            ToBuffer(ctx, params[2], params[3], entry.map.c_str());
            ToBuffer(ctx, params[4], params[5], entry.reason.c_str());
            cell_t* start;
            ctx->LocalToPhysAddr(params[6], &start);
            *start = static_cast<cell_t>(entry.startTime);
            return true;
        }

        // ---- SourceMod -----------------------------------------------------------

        // native void GetMapHistory(int item, char[] map, int mapLen, char[] reason, int reasonLen, int &startTime);
        cell_t N_SMGetMapHistory(IPluginContext* ctx, const cell_t* params)
        {
            if (!MapHistory(ctx, params))
                return ctx->ThrowNativeError("Invalid Map History Item requested.");
            return 0;
        }

        // native void ForceChangeLevel(const char[] map, const char[] reason);
        cell_t N_ForceChangeLevel(IPluginContext* ctx, const cell_t* params)
        {
            N_ChangeLevel(ctx, params);
            return 0;
        }

        cell_t ListSerial(const std::vector<std::string>& list)
        {
            size_t h = 0;
            for (const std::string& map : list)
                h = h * 31 + std::hash<std::string>()(map);
            return static_cast<cell_t>(h & 0x7fffffff);
        }

        // native Handle ReadMapList(Handle array=INVALID_HANDLE, int &serial=-1, const char[] str="default", int flags=MAPLIST_FLAG_CLEARARRAY);
        cell_t N_ReadMapList(IPluginContext* ctx, const cell_t* params)
        {
            constexpr cell_t kClearArray = 1 << 1;
            const cell_t flags = params[4];
            cell_t* serial;
            ctx->LocalToPhysAddr(params[2], &serial);

            std::vector<std::string> list;
            if (!Maps().ReadMapList(LocalString(ctx, params[3]), list, flags & ~kClearArray))
            {
                *serial = -1;
                return 0;
            }

            const cell_t current = ListSerial(list);
            // The caller's array already holds this very list.
            if (params[1] != 0 && *serial == current)
                return params[1];

            const cell_t handle = FillStringList(ctx, params[1], list, (flags & kClearArray) != 0, kMapBlockSize, CallerId(ctx));
            if (handle)
                *serial = current;
            return handle;
        }

        // native void SetMapListCompatBind(const char[] name, const char[] file);
        cell_t N_SetMapListCompatBind(IPluginContext*, const cell_t*)
        {
            // maplists.json is the only source; there is no old-style file to bind.
            return 0;
        }

        // ---- Source2ModMaps ------------------------------------------------------

        cell_t N_MapsGetMapHistory(IPluginContext* ctx, const cell_t* params)
        {
            return MapHistory(ctx, params);
        }

        // public static native ArrayList ReadMapList(const char[] name="default", int flags=0, ArrayList list=null);
        cell_t N_MapsReadMapList(IPluginContext* ctx, const cell_t* params)
        {
            std::vector<std::string> list;
            if (!Maps().ReadMapList(LocalString(ctx, params[1]), list, params[2]))
                return 0;
            return FillStringList(ctx, params[3], list, false, kMapBlockSize, CallerId(ctx));
        }

        const sp_nativeinfo_t g_mapNatives[] = {
            // halflife.inc / nextmap.inc / source2mod.inc
            { "GetCurrentMap", N_GetCurrentMap },
            { "IsMapValid", N_IsMapValid },
            { "FindMap", N_FindMap },
            { "GetMapDisplayName", N_GetMapDisplayName },
            { "SetNextMap", N_SetNextMap },
            { "GetNextMap", N_GetNextMap },
            { "ForceChangeLevel", N_ForceChangeLevel },
            { "GetMapHistorySize", N_GetMapHistorySize },
            { "GetMapHistory", N_SMGetMapHistory },
            { "ReadMapList", N_ReadMapList },
            { "SetMapListCompatBind", N_SetMapListCompatBind },

            // source2mod/maps.inc
            { "Source2ModMaps.GetCurrentMap", N_GetCurrentMap },
            { "Source2ModMaps.IsMapValid", N_IsMapValid },
            { "Source2ModMaps.FindMap", N_FindMap },
            { "Source2ModMaps.SetNextMap", N_SetNextMap },
            { "Source2ModMaps.GetNextMap", N_GetNextMap },
            { "Source2ModMaps.ChangeLevel", N_ChangeLevel },
            { "Source2ModMaps.GetMapHistorySize", N_GetMapHistorySize },
            { "Source2ModMaps.GetMapHistory", N_MapsGetMapHistory },
            { "Source2ModMaps.ReadMapList", N_MapsReadMapList },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetMapNatives()
    {
        return g_mapNatives;
    }
}
