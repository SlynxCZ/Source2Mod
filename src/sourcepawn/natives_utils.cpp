/**
* vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * Copyright (C) 2025-2026 Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl.
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
 * As a special exception, Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
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
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */

/*
 * SourcePawn natives for source2mod/utils.inc: Source2ModUtils (text, time,
 * sorting, logging), Source2ModRegex and Source2ModProfiler, on
 * ISource2ModUtils.
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/utils.h"

#include <sp_callable.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <random>
#include <string>
#include <unordered_map>
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

        utils::UtilsManager& Utils()
        {
            return utils::utilsManager;
        }

        cell_t ToBuffer(IPluginContext* ctx, cell_t addr, cell_t maxlen, const char* value)
        {
            size_t written = 0;
            ctx->StringToLocalUTF8(addr, maxlen, value ? value : "", &written);
            return static_cast<cell_t>(written);
        }

        constexpr cell_t kSortAscending = 0;
        constexpr cell_t kSortDescending = 1;
        constexpr cell_t kSortRandom = 2;

        std::mt19937& Rng()
        {
            static std::mt19937 rng{ std::random_device{}() };
            return rng;
        }

        // ---- text -----------------------------------------------------------------------

        // Explode(text, separator, maxlen, maxParts)
        cell_t N_Explode(IPluginContext* ctx, const cell_t* params)
        {
            const cell_t maxlen = params[0] >= 3 ? params[3] : 256;
            if (maxlen < 1)
                return ctx->ThrowNativeError("Invalid maxlen %d", maxlen);
            std::vector<std::string> parts = Utils().Explode(LocalString(ctx, params[1]), LocalString(ctx, params[2]), params[0] >= 4 ? params[4] : 0);
            const cell_t block = static_cast<cell_t>((maxlen + sizeof(cell_t) - 1) / sizeof(cell_t));
            return FillStringList(ctx, 0, parts, false, block, CallerId(ctx));
        }

        // Implode(parts, separator, buffer, maxlen)
        cell_t N_Implode(IPluginContext* ctx, const cell_t* params)
        {
            std::vector<std::string> parts;
            if (!ReadStringList(ctx, params[1], parts))
                return 0;
            return ToBuffer(ctx, params[3], params[4], Utils().Implode(parts, LocalString(ctx, params[2])).c_str());
        }

        // Replace(text, maxlen, search, replace, caseSensitive)
        cell_t N_Replace(IPluginContext* ctx, const cell_t* params)
        {
            std::string text = LocalString(ctx, params[1]);
            const int count = Utils().Replace(text, LocalString(ctx, params[3]), LocalString(ctx, params[4]), params[0] < 5 || params[5]);
            ToBuffer(ctx, params[1], params[2], text.c_str());
            return count;
        }

        cell_t N_Trim(IPluginContext* ctx, const cell_t* params)
        {
            return ToBuffer(ctx, params[2], params[3], Utils().Trim(LocalString(ctx, params[1])).c_str());
        }

        cell_t N_Find(IPluginContext* ctx, const cell_t* params)
        {
            const std::string text = LocalString(ctx, params[1]);
            return Utils().Find(text.c_str(), LocalString(ctx, params[2]), params[0] < 3 || params[3]);
        }

        cell_t N_Equal(IPluginContext* ctx, const cell_t* params)
        {
            const std::string a = LocalString(ctx, params[1]);
            return Utils().StrEqual(a.c_str(), LocalString(ctx, params[2]), params[0] < 3 || params[3]);
        }

        // ---- time -----------------------------------------------------------------------

        cell_t N_GetTime(IPluginContext*, const cell_t*)
        {
            return static_cast<cell_t>(Utils().GetTime());
        }

        // FormatTime(buffer, maxlen, format, stamp)
        cell_t N_FormatTime(IPluginContext* ctx, const cell_t* params)
        {
            const int64_t stamp = params[0] >= 4 ? params[4] : -1;
            return ToBuffer(ctx, params[1], params[2], Utils().FormatTime(LocalString(ctx, params[3]), stamp).c_str());
        }

        cell_t N_ParseTime(IPluginContext* ctx, const cell_t* params)
        {
            const std::string text = LocalString(ctx, params[1]);
            return static_cast<cell_t>(Utils().ParseTime(text.c_str(), LocalString(ctx, params[2])));
        }

        cell_t N_GetTickCount(IPluginContext*, const cell_t*)
        {
            return static_cast<cell_t>(Utils().GetTickCount());
        }

        // LogToFile(path, format, ...)
        cell_t N_LogToFile(IPluginContext* ctx, const cell_t* params)
        {
            std::vector<char> message(8192);
            if (!FormatParams(ctx, params, 2, message.data(), message.size()))
                return 0;
            return Utils().LogToFile(CallerId(ctx), LocalString(ctx, params[1]), message.data());
        }

        // ---- sorting --------------------------------------------------------------------

        template <typename Less>
        void SortCells(cell_t* array, cell_t size, cell_t order, Less less)
        {
            if (order == kSortRandom)
                std::shuffle(array, array + size, Rng());
            else if (order == kSortDescending)
                std::stable_sort(array, array + size, [&](cell_t a, cell_t b) { return less(b, a); });
            else
                std::stable_sort(array, array + size, less);
        }

        // SortInts(array, size, order)
        cell_t N_SortInts(IPluginContext* ctx, const cell_t* params)
        {
            cell_t* array;
            ctx->LocalToPhysAddr(params[1], &array);
            if (params[2] < 0)
                return ctx->ThrowNativeError("Invalid array size %d", params[2]);
            SortCells(array, params[2], params[0] >= 3 ? params[3] : kSortAscending, [](cell_t a, cell_t b) { return a < b; });
            return 0;
        }

        cell_t N_SortFloats(IPluginContext* ctx, const cell_t* params)
        {
            cell_t* array;
            ctx->LocalToPhysAddr(params[1], &array);
            if (params[2] < 0)
                return ctx->ThrowNativeError("Invalid array size %d", params[2]);
            SortCells(array, params[2], params[0] >= 3 ? params[3] : kSortAscending, [](cell_t a, cell_t b) { return sp_ctof(a) < sp_ctof(b); });
            return 0;
        }

        // SortStrings(array, count, order): reorders the rows, not their text.
        cell_t N_SortStrings(IPluginContext* ctx, const cell_t* params)
        {
            const cell_t count = params[2];
            if (count < 0)
                return ctx->ThrowNativeError("Invalid array size %d", count);
            cell_t* iv;
            ctx->LocalToPhysAddr(params[1], &iv);
            const bool direct = ctx->GetRuntime()->UsesDirectArrays();

            // Each row's address: absolute with direct arrays, else relative
            // to its own slot in the indirection vector.
            std::vector<cell_t> rows(count);
            for (cell_t i = 0; i < count; i++)
                rows[i] = direct ? iv[i] : params[1] + i * static_cast<cell_t>(sizeof(cell_t)) + iv[i];

            auto less = [ctx](cell_t a, cell_t b) { return std::strcmp(LocalString(ctx, a), LocalString(ctx, b)) < 0; };
            SortCells(rows.data(), count, params[0] >= 3 ? params[3] : kSortAscending, less);

            for (cell_t i = 0; i < count; i++)
                iv[i] = direct ? rows[i] : rows[i] - (params[1] + i * static_cast<cell_t>(sizeof(cell_t)));
            return 0;
        }

        // SortCustom(array, size, func, data): func(a, b, array, data)
        cell_t N_SortCustom(IPluginContext* ctx, const cell_t* params)
        {
            const cell_t size = params[2];
            if (size < 0)
                return ctx->ThrowNativeError("Invalid array size %d", size);
            IPluginFunction* fn = ctx->GetFunctionById(static_cast<funcid_t>(params[3]));
            if (!fn)
                return ctx->ThrowNativeError("Invalid function id (%X)", params[3]);
            cell_t* array;
            ctx->LocalToPhysAddr(params[1], &array);
            const cell_t data = params[0] >= 4 ? params[4] : 0;

            // The callback sees the array as it was; the sorted copy goes back after.
            std::vector<cell_t> values(array, array + size);
            bool failed = false;
            std::stable_sort(values.begin(), values.end(), [&](cell_t a, cell_t b)
            {
                if (failed)
                    return false;
                sp::CallArgs args;
                args.PushCell(a);
                args.PushCell(b);
                args.PushArray(array, static_cast<unsigned int>(size));
                args.PushCell(data);
                cell_t result = 0;
                if (!Invoke(fn, args, &result))
                {
                    failed = true;
                    return false;
                }
                return result < 0;
            });
            if (!failed)
                std::copy(values.begin(), values.end(), array);
            return 0;
        }

        // ---- Source2ModRegex ------------------------------------------------------------

        struct RegexObj : HandleObject
        {
            ISource2ModRegex* re = nullptr;
            ~RegexObj() override
            {
                if (re)
                    re->Release();
            }
        };

        HandleType RegexType()
        {
            static const HandleType t = Handles().CreateType("Source2ModRegex", [](void* p) { delete static_cast<RegexObj*>(p); });
            return t;
        }

        ISource2ModRegex* RegexOf(IPluginContext* ctx, cell_t handle)
        {
            auto* obj = static_cast<RegexObj*>(ReadHandle(ctx, handle, RegexType()));
            return obj ? obj->re : nullptr;
        }

        // Compile(pattern, flags, error, maxlen)
        cell_t N_RegexCompile(IPluginContext* ctx, const cell_t* params)
        {
            std::string error;
            ISource2ModRegex* re = Utils().CompileRegex(LocalString(ctx, params[1]), params[0] >= 2 ? params[2] : 0, &error);
            if (!re)
            {
                if (params[0] >= 4)
                    ToBuffer(ctx, params[3], params[4], error.c_str());
                return 0;
            }
            auto* obj = new RegexObj();
            obj->re = re;
            const cell_t h = Handles().Create(RegexType(), obj, CallerId(ctx));
            if (!h)
                delete obj;
            return h;
        }

        cell_t N_RegexMatch(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModRegex* re = RegexOf(ctx, params[1]);
            if (!re)
                return 0;
            const std::string text = LocalString(ctx, params[2]);
            const cell_t offset = params[0] >= 3 ? params[3] : 0;
            if (offset < 0 || static_cast<size_t>(offset) > text.size())
                return ctx->ThrowNativeError("Offset %d is outside the text", offset);
            return re->Match(text.c_str(), static_cast<size_t>(offset));
        }

        cell_t N_RegexMatchAll(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModRegex* re = RegexOf(ctx, params[1]);
            return re ? re->MatchAll(LocalString(ctx, params[2])) : 0;
        }

        cell_t N_RegexMatchCount(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModRegex* re = RegexOf(ctx, params[1]);
            return re ? re->MatchCount() : 0;
        }

        cell_t N_RegexCaptureCount(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModRegex* re = RegexOf(ctx, params[1]);
            return re ? re->CaptureCount(params[0] >= 2 ? params[2] : 0) : 0;
        }

        // GetSubString(index, buffer, maxlen, match)
        cell_t N_RegexGetSubString(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModRegex* re = RegexOf(ctx, params[1]);
            if (!re)
                return 0;
            const char* sub = re->GetSubString(params[2], params[0] >= 5 ? params[5] : 0);
            if (!sub)
                return 0;
            ToBuffer(ctx, params[3], params[4], sub);
            return 1;
        }

        cell_t N_RegexMatchEnd(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModRegex* re = RegexOf(ctx, params[1]);
            return re ? static_cast<cell_t>(re->MatchEnd(params[0] >= 2 ? params[2] : 0)) : 0;
        }

        // ---- Source2ModProfiler ---------------------------------------------------------

        struct ProfilerObj : HandleObject
        {
            using Clock = std::chrono::steady_clock;
            Clock::time_point start{};
            Clock::time_point stop{};
            bool running = false;
        };

        HandleType ProfilerType()
        {
            static const HandleType t = Handles().CreateType("Source2ModProfiler", [](void* p) { delete static_cast<ProfilerObj*>(p); });
            return t;
        }

        ProfilerObj* ProfilerOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<ProfilerObj*>(ReadHandle(ctx, handle, ProfilerType()));
        }

        cell_t N_ProfilerNew(IPluginContext* ctx, const cell_t*)
        {
            auto* obj = new ProfilerObj();
            const cell_t h = Handles().Create(ProfilerType(), obj, CallerId(ctx));
            if (!h)
                delete obj;
            return h;
        }

        cell_t N_ProfilerStart(IPluginContext* ctx, const cell_t* params)
        {
            if (ProfilerObj* p = ProfilerOf(ctx, params[1]))
            {
                p->start = ProfilerObj::Clock::now();
                p->running = true;
            }
            return 0;
        }

        cell_t N_ProfilerStop(IPluginContext* ctx, const cell_t* params)
        {
            if (ProfilerObj* p = ProfilerOf(ctx, params[1]))
            {
                p->stop = ProfilerObj::Clock::now();
                p->running = false;
            }
            return 0;
        }

        cell_t N_ProfilerTime(IPluginContext* ctx, const cell_t* params)
        {
            ProfilerObj* p = ProfilerOf(ctx, params[1]);
            if (!p)
                return sp_ftoc(0.0f);
            const auto end = p->running ? ProfilerObj::Clock::now() : p->stop;
            return sp_ftoc(std::chrono::duration<float>(end - p->start).count());
        }

        // ---- IntMap / IntMapSnapshot (StringMap's shape with int keys) ------------------

        struct IntEntry
        {
            enum Kind { Cell, Array, String } kind = Cell;
            cell_t cell = 0;
            std::vector<cell_t> array;
            std::string string;
        };

        struct IntMapObj : HandleObject
        {
            std::unordered_map<cell_t, IntEntry> map;
        };

        struct IntSnapshotObj : HandleObject
        {
            std::vector<cell_t> keys;
        };

        HandleType IntMapType()
        {
            static const HandleType t = Handles().CreateType("IntMap", [](void* p) { delete static_cast<IntMapObj*>(p); });
            return t;
        }

        HandleType IntSnapshotType()
        {
            static const HandleType t = Handles().CreateType("IntMapSnapshot", [](void* p) { delete static_cast<IntSnapshotObj*>(p); });
            return t;
        }

        IntMapObj* IntMapOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<IntMapObj*>(ReadHandle(ctx, handle, IntMapType()));
        }

        template <typename T>
        cell_t NewObject(IPluginContext* ctx, HandleType type, T* obj)
        {
            const cell_t h = Handles().Create(type, obj, CallerId(ctx));
            if (!h)
            {
                delete obj;
                return ctx->ThrowNativeError("Handle limit reached");
            }
            return h;
        }

        cell_t N_IntMapNew(IPluginContext* ctx, const cell_t*)
        {
            return NewObject(ctx, IntMapType(), new IntMapObj());
        }

        cell_t N_IntMapClone(IPluginContext* ctx, const cell_t* params)
        {
            IntMapObj* map = IntMapOf(ctx, params[1]);
            if (!map)
                return 0;
            auto* copy = new IntMapObj();
            copy->map = map->map;
            return NewObject(ctx, IntMapType(), copy);
        }

        // Where a Set* writes: nullptr when the key is taken and replace is false.
        IntEntry* IntSlot(IntMapObj* map, cell_t key, bool replace)
        {
            auto [it, inserted] = map->map.try_emplace(key);
            if (!inserted && !replace)
                return nullptr;
            it->second = IntEntry{};
            return &it->second;
        }

        // SetValue(key, value, replace=true)
        cell_t N_IntMapSetValue(IPluginContext* ctx, const cell_t* params)
        {
            IntMapObj* map = IntMapOf(ctx, params[1]);
            IntEntry* e = map ? IntSlot(map, params[2], params[0] < 4 || params[4]) : nullptr;
            if (!e)
                return 0;
            e->kind = IntEntry::Cell;
            e->cell = params[3];
            return 1;
        }

        // SetArray(key, array, num_items, replace=true)
        cell_t N_IntMapSetArray(IPluginContext* ctx, const cell_t* params)
        {
            IntMapObj* map = IntMapOf(ctx, params[1]);
            if (!map)
                return 0;
            if (params[4] < 0)
                return ctx->ThrowNativeError("Invalid array size: %d", params[4]);
            cell_t* src;
            ctx->LocalToPhysAddr(params[3], &src);
            IntEntry* e = IntSlot(map, params[2], params[0] < 5 || params[5]);
            if (!e)
                return 0;
            e->kind = IntEntry::Array;
            e->array.assign(src, src + params[4]);
            return 1;
        }

        // SetString(key, value, replace=true)
        cell_t N_IntMapSetString(IPluginContext* ctx, const cell_t* params)
        {
            IntMapObj* map = IntMapOf(ctx, params[1]);
            if (!map)
                return 0;
            const std::string value = LocalString(ctx, params[3]);
            IntEntry* e = IntSlot(map, params[2], params[0] < 4 || params[4]);
            if (!e)
                return 0;
            e->kind = IntEntry::String;
            e->string = value;
            return 1;
        }

        const IntEntry* IntFind(IntMapObj* map, cell_t key)
        {
            auto it = map->map.find(key);
            return it != map->map.end() ? &it->second : nullptr;
        }

        void WriteSize(IPluginContext* ctx, const cell_t* params, int index, size_t value)
        {
            cell_t* out;
            if (params[0] >= index && ctx->LocalToPhysAddr(params[index], &out) == SP_ERROR_NONE)
                *out = static_cast<cell_t>(value);
        }

        // GetValue(key, &value): a one-cell array reads as a value, as with StringMap.
        cell_t N_IntMapGetValue(IPluginContext* ctx, const cell_t* params)
        {
            IntMapObj* map = IntMapOf(ctx, params[1]);
            const IntEntry* e = map ? IntFind(map, params[2]) : nullptr;
            if (!e)
                return 0;
            cell_t value;
            if (e->kind == IntEntry::Cell)
                value = e->cell;
            else if (e->kind == IntEntry::Array && e->array.size() == 1)
                value = e->array[0];
            else
                return 0;
            cell_t* addr;
            ctx->LocalToPhysAddr(params[3], &addr);
            *addr = value;
            return 1;
        }

        // GetArray(key, array, max_size, &size=0)
        cell_t N_IntMapGetArray(IPluginContext* ctx, const cell_t* params)
        {
            IntMapObj* map = IntMapOf(ctx, params[1]);
            if (!map)
                return 0;
            if (params[4] < 0)
                return ctx->ThrowNativeError("Invalid array size: %d", params[4]);
            const IntEntry* e = IntFind(map, params[2]);
            if (!e || e->kind == IntEntry::String)
                return 0;
            cell_t* dest;
            ctx->LocalToPhysAddr(params[3], &dest);
            size_t count;
            if (e->kind == IntEntry::Cell)
            {
                count = params[4] > 0 ? 1 : 0;
                if (count)
                    dest[0] = e->cell;
            }
            else
            {
                count = std::min(e->array.size(), static_cast<size_t>(params[4]));
                std::copy_n(e->array.data(), count, dest);
            }
            WriteSize(ctx, params, 5, count);
            return 1;
        }

        // GetString(key, value, max_size, &size=0)
        cell_t N_IntMapGetString(IPluginContext* ctx, const cell_t* params)
        {
            IntMapObj* map = IntMapOf(ctx, params[1]);
            if (!map)
                return 0;
            if (params[4] < 0)
                return ctx->ThrowNativeError("Invalid buffer size: %d", params[4]);
            const IntEntry* e = IntFind(map, params[2]);
            if (!e || e->kind != IntEntry::String)
                return 0;
            size_t written = 0;
            ctx->StringToLocalUTF8(params[3], params[4], e->string.c_str(), &written);
            WriteSize(ctx, params, 5, written);
            return 1;
        }

        cell_t N_IntMapContainsKey(IPluginContext* ctx, const cell_t* params)
        {
            IntMapObj* map = IntMapOf(ctx, params[1]);
            return map && IntFind(map, params[2]);
        }

        cell_t N_IntMapRemove(IPluginContext* ctx, const cell_t* params)
        {
            IntMapObj* map = IntMapOf(ctx, params[1]);
            return map && map->map.erase(params[2]) > 0;
        }

        cell_t N_IntMapClear(IPluginContext* ctx, const cell_t* params)
        {
            if (IntMapObj* map = IntMapOf(ctx, params[1]))
                map->map.clear();
            return 0;
        }

        cell_t N_IntMapSize(IPluginContext* ctx, const cell_t* params)
        {
            IntMapObj* map = IntMapOf(ctx, params[1]);
            return map ? static_cast<cell_t>(map->map.size()) : 0;
        }

        cell_t N_IntMapSnapshot(IPluginContext* ctx, const cell_t* params)
        {
            IntMapObj* map = IntMapOf(ctx, params[1]);
            if (!map)
                return 0;
            auto* snap = new IntSnapshotObj();
            snap->keys.reserve(map->map.size());
            for (const auto& [key, value] : map->map)
                snap->keys.push_back(key);
            return NewObject(ctx, IntSnapshotType(), snap);
        }

        cell_t N_IntSnapshotLength(IPluginContext* ctx, const cell_t* params)
        {
            auto* snap = static_cast<IntSnapshotObj*>(ReadHandle(ctx, params[1], IntSnapshotType()));
            return snap ? static_cast<cell_t>(snap->keys.size()) : 0;
        }

        cell_t N_IntSnapshotGetKey(IPluginContext* ctx, const cell_t* params)
        {
            auto* snap = static_cast<IntSnapshotObj*>(ReadHandle(ctx, params[1], IntSnapshotType()));
            if (!snap)
                return 0;
            if (params[2] < 0 || static_cast<size_t>(params[2]) >= snap->keys.size())
                return ctx->ThrowNativeError("Invalid index %d", params[2]);
            return snap->keys[params[2]];
        }

        const sp_nativeinfo_t g_utilNatives[] = {
            { "IntMap.IntMap", N_IntMapNew },
            { "IntMap.Clone", N_IntMapClone },
            { "IntMap.SetValue", N_IntMapSetValue },
            { "IntMap.SetArray", N_IntMapSetArray },
            { "IntMap.SetString", N_IntMapSetString },
            { "IntMap.GetValue", N_IntMapGetValue },
            { "IntMap.GetArray", N_IntMapGetArray },
            { "IntMap.GetString", N_IntMapGetString },
            { "IntMap.ContainsKey", N_IntMapContainsKey },
            { "IntMap.Remove", N_IntMapRemove },
            { "IntMap.Clear", N_IntMapClear },
            { "IntMap.Snapshot", N_IntMapSnapshot },
            { "IntMap.Size.get", N_IntMapSize },
            { "IntMapSnapshot.Length.get", N_IntSnapshotLength },
            { "IntMapSnapshot.GetKey", N_IntSnapshotGetKey },
            { "Source2ModUtils.Explode", N_Explode },
            { "Source2ModUtils.Implode", N_Implode },
            { "Source2ModUtils.Replace", N_Replace },
            { "Source2ModUtils.Trim", N_Trim },
            { "Source2ModUtils.Find", N_Find },
            { "Source2ModUtils.Equal", N_Equal },
            { "Source2ModUtils.GetTime", N_GetTime },
            { "Source2ModUtils.FormatTime", N_FormatTime },
            { "Source2ModUtils.ParseTime", N_ParseTime },
            { "Source2ModUtils.GetTickCount", N_GetTickCount },
            { "Source2ModUtils.LogToFile", N_LogToFile },
            { "Source2ModUtils.SortInts", N_SortInts },
            { "Source2ModUtils.SortFloats", N_SortFloats },
            { "Source2ModUtils.SortStrings", N_SortStrings },
            { "Source2ModUtils.SortCustom", N_SortCustom },
            { "Source2ModRegex.Compile", N_RegexCompile },
            { "Source2ModRegex.Match", N_RegexMatch },
            { "Source2ModRegex.MatchAll", N_RegexMatchAll },
            { "Source2ModRegex.MatchCount", N_RegexMatchCount },
            { "Source2ModRegex.CaptureCount", N_RegexCaptureCount },
            { "Source2ModRegex.GetSubString", N_RegexGetSubString },
            { "Source2ModRegex.MatchEnd", N_RegexMatchEnd },
            { "Source2ModProfiler.Source2ModProfiler", N_ProfilerNew },
            { "Source2ModProfiler.Start", N_ProfilerStart },
            { "Source2ModProfiler.Stop", N_ProfilerStop },
            { "Source2ModProfiler.Time.get", N_ProfilerTime },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetUtilNatives()
    {
        return g_utilNatives;
    }
}
