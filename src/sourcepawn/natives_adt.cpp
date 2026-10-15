/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * ADT natives: ArrayList (adt_array.inc), ArrayStack (adt_stack.inc),
 * StringMap and StringMapSnapshot (adt_trie.inc) and DataPack (datapack.inc).
 * SourceMod's smn_adt_array.cpp, smn_adt_stack.cpp, smn_adt_trie.cpp,
 * smn_datapacks.cpp and CDataPack (AlliedModders LLC) are the reference for
 * the behaviour -- block layout, bounds checks, error wording, the methodmap
 * and old-style names sharing one implementation.
 *
 * Project: Source2Mod
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include <sp_callable.h>

#include <algorithm>
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
        PluginId OwnerOfCaller(IPluginContext* ctx)
        {
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            return plugin ? plugin->Id() : 0;
        }

        cell_t NewHandle(IPluginContext* ctx, HandleType type, HandleObject* object)
        {
            const cell_t handle = Handles().Create(type, object, OwnerOfCaller(ctx));
            if (!handle)
            {
                delete object;
                return ctx->ThrowNativeError("Handle limit reached");
            }
            return handle;
        }

        // ---- CellArray: ArrayList and ArrayStack ---------------------------------

        // `blocksize` cells per item, back to back.
        struct CellArray : HandleObject
        {
            explicit CellArray(size_t blocksize) : blocksize(blocksize) {}

            size_t blocksize;
            std::vector<cell_t> data;

            size_t size() const { return data.size() / blocksize; }
            cell_t* at(size_t index) { return data.data() + index * blocksize; }
            cell_t* push()
            {
                data.resize(data.size() + blocksize, 0);
                return at(size() - 1);
            }
            void resize(size_t items) { data.resize(items * blocksize, 0); }
        };

        CellArray* ArrayOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<CellArray*>(ReadHandle(ctx, handle, Types().arrayList));
        }

        CellArray* StackOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<CellArray*>(ReadHandle(ctx, handle, Types().arrayStack));
        }

        bool CheckIndex(IPluginContext* ctx, CellArray* array, cell_t index)
        {
            if (index < 0 || static_cast<size_t>(index) >= array->size())
            {
                ctx->ThrowNativeError("Invalid index %d (count: %d)", index, static_cast<int>(array->size()));
                return false;
            }
            return true;
        }

        // A cell of an item, or with asChar a byte of it.
        bool ReadBlock(IPluginContext* ctx, CellArray* array, cell_t* blk, cell_t block, bool asChar, cell_t& out)
        {
            if (asChar)
            {
                if (block < 0 || static_cast<size_t>(block) >= array->blocksize * sizeof(cell_t))
                {
                    ctx->ThrowNativeError("Invalid byte %d (blocksize: %d bytes)", block, static_cast<int>(array->blocksize * sizeof(cell_t)));
                    return false;
                }
                out = static_cast<cell_t>(reinterpret_cast<char*>(blk)[block]);
                return true;
            }
            if (block < 0 || static_cast<size_t>(block) >= array->blocksize)
            {
                ctx->ThrowNativeError("Invalid block %d (blocksize: %d)", block, static_cast<int>(array->blocksize));
                return false;
            }
            out = blk[block];
            return true;
        }

        // A string into an item: the whole block is the buffer.
        size_t WriteStringBlock(CellArray* array, cell_t* blk, size_t block, const char* str, size_t maxBytes)
        {
            char* dest = reinterpret_cast<char*>(blk + block);
            const size_t room = std::min((array->blocksize - block) * sizeof(cell_t), maxBytes);
            if (room == 0)
                return 0;
            const size_t len = std::min(strlen(str), room - 1);
            memcpy(dest, str, len);
            dest[len] = '\0';
            return len;
        }

        size_t WriteArrayBlock(IPluginContext* ctx, CellArray* array, cell_t* blk, size_t block, cell_t addr, cell_t size)
        {
            cell_t* src;
            ctx->LocalToPhysAddr(addr, &src);
            size_t count = array->blocksize - block;
            if (size >= 0 && static_cast<size_t>(size) < count)
                count = static_cast<size_t>(size);
            memcpy(blk + block, src, count * sizeof(cell_t));
            return count;
        }

        cell_t N_ArrayList(IPluginContext* ctx, const cell_t* params)
        {
            if (params[1] < 1)
                return ctx->ThrowNativeError("Invalid block size (must be > 0)");
            if (params[2] < 0)
                return ctx->ThrowNativeError("Failed to resize array to startsize \"%u\".", params[2]);

            auto* array = new CellArray(static_cast<size_t>(params[1]));
            array->resize(static_cast<size_t>(params[2]));
            return NewHandle(ctx, Types().arrayList, array);
        }

        cell_t N_ArrayClear(IPluginContext* ctx, const cell_t* params)
        {
            if (CellArray* array = ArrayOf(ctx, params[1]))
                array->data.clear();
            return 0;
        }

        cell_t N_ArrayClone(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* array = ArrayOf(ctx, params[1]);
            if (!array)
                return 0;
            auto* copy = new CellArray(array->blocksize);
            copy->data = array->data;
            return NewHandle(ctx, Types().arrayList, copy);
        }

        cell_t N_ArrayResize(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* array = ArrayOf(ctx, params[1]);
            if (!array)
                return 0;
            if (params[2] < 0)
                return ctx->ThrowNativeError("Unable to resize array to \"%u\"", params[2]);
            array->resize(static_cast<size_t>(params[2]));
            return 0;
        }

        cell_t N_ArrayLength(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* array = ArrayOf(ctx, params[1]);
            return array ? static_cast<cell_t>(array->size()) : 0;
        }

        cell_t N_ArrayBlockSize(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* array = ArrayOf(ctx, params[1]);
            return array ? static_cast<cell_t>(array->blocksize) : 0;
        }

        cell_t N_ArrayPush(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* array = ArrayOf(ctx, params[1]);
            if (!array)
                return 0;
            array->push()[0] = params[2];
            return static_cast<cell_t>(array->size() - 1);
        }

        cell_t N_ArrayPushString(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* array = ArrayOf(ctx, params[1]);
            if (!array)
                return 0;
            WriteStringBlock(array, array->push(), 0, LocalString(ctx, params[2]), SIZE_MAX);
            return static_cast<cell_t>(array->size() - 1);
        }

        cell_t N_ArrayPushArray(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* array = ArrayOf(ctx, params[1]);
            if (!array)
                return 0;
            WriteArrayBlock(ctx, array, array->push(), 0, params[2], params[0] >= 3 ? params[3] : -1);
            return static_cast<cell_t>(array->size() - 1);
        }

        // Get(index, block=0, asChar=false)
        cell_t N_ArrayGet(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* array = ArrayOf(ctx, params[1]);
            if (!array || !CheckIndex(ctx, array, params[2]))
                return 0;
            cell_t out = 0;
            ReadBlock(ctx, array, array->at(params[2]), params[0] >= 3 ? params[3] : 0, params[0] >= 4 && params[4], out);
            return out;
        }

        // GetString(index, buffer, maxlength, block=0)
        cell_t N_ArrayGetString(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* array = ArrayOf(ctx, params[1]);
            if (!array || !CheckIndex(ctx, array, params[2]))
                return 0;
            const cell_t block = params[0] >= 5 ? params[5] : 0;
            if (block < 0 || static_cast<size_t>(block) >= array->blocksize)
                return ctx->ThrowNativeError("Invalid block %d (blocksize: %d)", block, static_cast<int>(array->blocksize));

            // The block need not be terminated; never read past the item.
            cell_t* blk = array->at(params[2]) + block;
            const size_t room = (array->blocksize - block) * sizeof(cell_t);
            const std::string str(reinterpret_cast<const char*>(blk), strnlen(reinterpret_cast<const char*>(blk), room));
            size_t written = 0;
            ctx->StringToLocalUTF8(params[3], params[4], str.c_str(), &written);
            return static_cast<cell_t>(written);
        }

        // GetArray(index, buffer, size=-1, block=0)
        cell_t N_ArrayGetArray(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* array = ArrayOf(ctx, params[1]);
            if (!array || !CheckIndex(ctx, array, params[2]))
                return 0;
            const cell_t block = params[0] >= 5 ? params[5] : 0;
            if (block < 0 || static_cast<size_t>(block) >= array->blocksize)
                return ctx->ThrowNativeError("Invalid block %d (blocksize: %d)", block, static_cast<int>(array->blocksize));

            size_t count = array->blocksize - block;
            const cell_t size = params[0] >= 4 ? params[4] : -1;
            if (size >= 0 && static_cast<size_t>(size) < count)
                count = static_cast<size_t>(size);

            cell_t* dest;
            ctx->LocalToPhysAddr(params[3], &dest);
            memcpy(dest, array->at(params[2]) + block, count * sizeof(cell_t));
            return static_cast<cell_t>(count);
        }

        // Set(index, value, block=0, asChar=false)
        cell_t N_ArraySet(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* array = ArrayOf(ctx, params[1]);
            if (!array || !CheckIndex(ctx, array, params[2]))
                return 0;
            cell_t* blk = array->at(params[2]);
            const cell_t block = params[0] >= 4 ? params[4] : 0;
            const bool asChar = params[0] >= 5 && params[5];
            cell_t dummy;
            if (!ReadBlock(ctx, array, blk, block, asChar, dummy))
                return 0;
            if (asChar)
                reinterpret_cast<char*>(blk)[block] = static_cast<char>(params[3]);
            else
                blk[block] = params[3];
            return 0;
        }

        // SetString(index, value, size=-1, block=0)
        cell_t N_ArraySetString(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* array = ArrayOf(ctx, params[1]);
            if (!array || !CheckIndex(ctx, array, params[2]))
                return 0;
            const cell_t block = params[0] >= 5 ? params[5] : 0;
            if (block < 0 || static_cast<size_t>(block) >= array->blocksize)
                return ctx->ThrowNativeError("Invalid block %d (blocksize: %d)", block, static_cast<int>(array->blocksize));
            const cell_t size = params[0] >= 4 ? params[4] : -1;
            return static_cast<cell_t>(WriteStringBlock(array, array->at(params[2]), block, LocalString(ctx, params[3]),
                                                        size >= 0 ? static_cast<size_t>(size) : SIZE_MAX));
        }

        // SetArray(index, values, size=-1, block=0)
        cell_t N_ArraySetArray(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* array = ArrayOf(ctx, params[1]);
            if (!array || !CheckIndex(ctx, array, params[2]))
                return 0;
            const cell_t block = params[0] >= 5 ? params[5] : 0;
            if (block < 0 || static_cast<size_t>(block) >= array->blocksize)
                return ctx->ThrowNativeError("Invalid block %d (blocksize: %d)", block, static_cast<int>(array->blocksize));
            return static_cast<cell_t>(WriteArrayBlock(ctx, array, array->at(params[2]), block, params[3], params[0] >= 4 ? params[4] : -1));
        }

        cell_t N_ArrayShiftUp(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* array = ArrayOf(ctx, params[1]);
            if (!array || !CheckIndex(ctx, array, params[2]))
                return 0;
            array->data.insert(array->data.begin() + params[2] * array->blocksize, array->blocksize, 0);
            return 0;
        }

        cell_t N_ArrayErase(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* array = ArrayOf(ctx, params[1]);
            if (!array || !CheckIndex(ctx, array, params[2]))
                return 0;
            auto first = array->data.begin() + params[2] * array->blocksize;
            array->data.erase(first, first + array->blocksize);
            return 0;
        }

        cell_t N_ArraySwapAt(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* array = ArrayOf(ctx, params[1]);
            if (!array || !CheckIndex(ctx, array, params[2]) || !CheckIndex(ctx, array, params[3]))
                return 0;
            std::swap_ranges(array->at(params[2]), array->at(params[2]) + array->blocksize, array->at(params[3]));
            return 0;
        }

        // The start / reverse rules of SourceMod's FindStringInArray.
        template <typename MATCH>
        cell_t FindIn(IPluginContext* ctx, CellArray* array, const cell_t* params, MATCH match)
        {
            const size_t block = params[0] >= 3 ? static_cast<size_t>(params[3]) : 0;
            if (block >= array->blocksize)
                return ctx->ThrowNativeError("Invalid block %d (blocksize: %d)", static_cast<int>(block), static_cast<int>(array->blocksize));

            int start = params[0] >= 4 ? params[4] : -1;
            if (start > 0 && start > static_cast<int>(array->size()))
                return ctx->ThrowNativeError("Invalid start index %d (max: %d)", start, static_cast<int>(array->size()));
            const bool reverse = params[0] >= 5 && params[5];

            if (reverse)
            {
                if (start < 0)
                    start = static_cast<int>(array->size());
                for (int i = start - 1; i >= 0; i--)
                    if (match(array->at(i) + block))
                        return i;
            }
            else
            {
                if (start < -1)
                    start = -1;
                for (size_t i = static_cast<size_t>(start + 1); i < array->size(); i++)
                    if (match(array->at(i) + block))
                        return static_cast<cell_t>(i);
            }
            return -1;
        }

        // FindString(item, block=0, start=-1, reverse=false, caseSensitive=true)
        cell_t N_ArrayFindString(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* array = ArrayOf(ctx, params[1]);
            if (!array)
                return 0;
            const char* item = LocalString(ctx, params[2]);
            const bool caseSensitive = params[0] < 6 || params[6];
            return FindIn(ctx, array, params, [&](const cell_t* blk)
            {
                const char* s = reinterpret_cast<const char*>(blk);
                if (caseSensitive)
                    return strcmp(item, s) == 0;
#ifdef _WIN32
                return _stricmp(item, s) == 0;
#else
                return strcasecmp(item, s) == 0;
#endif
            });
        }

        // FindValue(item, block=0, start=-1, reverse=false)
        cell_t N_ArrayFindValue(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* array = ArrayOf(ctx, params[1]);
            if (!array)
                return 0;
            const cell_t item = params[2];
            return FindIn(ctx, array, params, [item](const cell_t* blk) { return *blk == item; });
        }

        // Reorders the items to follow `order` (item indexes).
        void Reorder(CellArray* array, const std::vector<size_t>& order)
        {
            std::vector<cell_t> sorted;
            sorted.reserve(array->data.size());
            for (size_t i : order)
                sorted.insert(sorted.end(), array->at(i), array->at(i) + array->blocksize);
            array->data = std::move(sorted);
        }

        // Sort(SortOrder order, SortType type): Sort_Ascending, Sort_Descending,
        // Sort_Random; Sort_Integer, Sort_Float, Sort_String.
        cell_t N_ArraySort(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* array = ArrayOf(ctx, params[1]);
            if (!array)
                return 0;
            const cell_t order = params[2];
            const cell_t type = params[3];

            std::vector<size_t> idx(array->size());
            for (size_t i = 0; i < idx.size(); i++)
                idx[i] = i;

            if (order == 2)
            {
                static std::mt19937 rng{ std::random_device{}() };
                std::shuffle(idx.begin(), idx.end(), rng);
            }
            else
            {
                auto less = [array, type](size_t a, size_t b)
                {
                    const cell_t* x = array->at(a);
                    const cell_t* y = array->at(b);
                    if (type == 1)
                        return sp_ctof(*x) < sp_ctof(*y);
                    if (type == 2)
                        return strcmp(reinterpret_cast<const char*>(x), reinterpret_cast<const char*>(y)) < 0;
                    return *x < *y;
                };
                std::stable_sort(idx.begin(), idx.end(), less);
                if (order == 1)
                    std::reverse(idx.begin(), idx.end());
            }

            Reorder(array, idx);
            return 0;
        }

        // SortCustom(SortFuncADTArray sortfunc, Handle hndl=INVALID_HANDLE):
        // int (int index1, int index2, Handle array, Handle hndl).
        cell_t N_ArraySortCustom(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* array = ArrayOf(ctx, params[1]);
            if (!array)
                return 0;
            IPluginFunction* fn = ctx->GetFunctionById(params[2]);
            if (!fn)
                return ctx->ThrowNativeError("Function %x is not a valid function", params[2]);

            std::vector<size_t> idx(array->size());
            for (size_t i = 0; i < idx.size(); i++)
                idx[i] = i;

            const cell_t handle = params[1];
            const cell_t extra = params[0] >= 3 ? params[3] : 0;
            bool failed = false;
            std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b)
            {
                if (failed)
                    return false;
                sp::CallArgs args;
                args.PushCell(static_cast<cell_t>(a));
                args.PushCell(static_cast<cell_t>(b));
                args.PushCell(handle);
                args.PushCell(extra);
                cell_t res = 0;
                if (!fn->Invoke(args, &res))
                {
                    failed = true;
                    return false;
                }
                return res < 0;
            });

            if (!failed)
                Reorder(array, idx);
            return 0;
        }

        // ---- ArrayStack ---------------------------------------------------------

        cell_t N_ArrayStack(IPluginContext* ctx, const cell_t* params)
        {
            const cell_t blocksize = params[0] >= 1 ? params[1] : 1;
            if (blocksize < 1)
                return ctx->ThrowNativeError("Invalid block size (must be > 0)");
            return NewHandle(ctx, Types().arrayStack, new CellArray(static_cast<size_t>(blocksize)));
        }

        cell_t N_StackClear(IPluginContext* ctx, const cell_t* params)
        {
            if (CellArray* stack = StackOf(ctx, params[1]))
                stack->data.clear();
            return 0;
        }

        cell_t N_StackClone(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* stack = StackOf(ctx, params[1]);
            if (!stack)
                return 0;
            auto* copy = new CellArray(stack->blocksize);
            copy->data = stack->data;
            return NewHandle(ctx, Types().arrayStack, copy);
        }

        cell_t N_StackPush(IPluginContext* ctx, const cell_t* params)
        {
            if (CellArray* stack = StackOf(ctx, params[1]))
                stack->push()[0] = params[2];
            return 0;
        }

        cell_t N_StackPushString(IPluginContext* ctx, const cell_t* params)
        {
            if (CellArray* stack = StackOf(ctx, params[1]))
                WriteStringBlock(stack, stack->push(), 0, LocalString(ctx, params[2]), SIZE_MAX);
            return 0;
        }

        cell_t N_StackPushArray(IPluginContext* ctx, const cell_t* params)
        {
            if (CellArray* stack = StackOf(ctx, params[1]))
                WriteArrayBlock(ctx, stack, stack->push(), 0, params[2], params[0] >= 3 ? params[3] : -1);
            return 0;
        }

        // Pop/Top(block=0, asChar=false): the methodmap form errors on an empty stack.
        cell_t StackCell(IPluginContext* ctx, const cell_t* params, bool pop)
        {
            CellArray* stack = StackOf(ctx, params[1]);
            if (!stack)
                return 0;
            if (stack->size() == 0)
                return ctx->ThrowNativeError("stack is empty");
            cell_t out = 0;
            if (!ReadBlock(ctx, stack, stack->at(stack->size() - 1), params[0] >= 2 ? params[2] : 0, params[0] >= 3 && params[3], out))
                return 0;
            if (pop)
                stack->resize(stack->size() - 1);
            return out;
        }

        cell_t N_StackPop(IPluginContext* ctx, const cell_t* params) { return StackCell(ctx, params, true); }
        cell_t N_StackTop(IPluginContext* ctx, const cell_t* params) { return StackCell(ctx, params, false); }

        // PopString/TopString(buffer, maxlength, &written=0)
        cell_t StackString(IPluginContext* ctx, const cell_t* params, bool pop)
        {
            CellArray* stack = StackOf(ctx, params[1]);
            if (!stack)
                return 0;
            if (stack->size() == 0)
                return ctx->ThrowNativeError("stack is empty");
            const cell_t* blk = stack->at(stack->size() - 1);
            const std::string str(reinterpret_cast<const char*>(blk), strnlen(reinterpret_cast<const char*>(blk), stack->blocksize * sizeof(cell_t)));
            size_t written = 0;
            ctx->StringToLocalUTF8(params[2], params[3], str.c_str(), &written);
            if (params[0] >= 4)
            {
                cell_t* out;
                if (ctx->LocalToPhysAddr(params[4], &out) == SP_ERROR_NONE)
                    *out = static_cast<cell_t>(written);
            }
            if (pop)
                stack->resize(stack->size() - 1);
            return 1;
        }

        cell_t N_StackPopString(IPluginContext* ctx, const cell_t* params) { return StackString(ctx, params, true); }
        cell_t N_StackTopString(IPluginContext* ctx, const cell_t* params) { return StackString(ctx, params, false); }

        // PopArray/TopArray(buffer, size=-1)
        cell_t StackArray(IPluginContext* ctx, const cell_t* params, bool pop)
        {
            CellArray* stack = StackOf(ctx, params[1]);
            if (!stack)
                return 0;
            if (stack->size() == 0)
                return ctx->ThrowNativeError("stack is empty");
            size_t count = stack->blocksize;
            const cell_t size = params[0] >= 3 ? params[3] : -1;
            if (size >= 0 && static_cast<size_t>(size) < count)
                count = static_cast<size_t>(size);
            cell_t* dest;
            ctx->LocalToPhysAddr(params[2], &dest);
            memcpy(dest, stack->at(stack->size() - 1), count * sizeof(cell_t));
            if (pop)
                stack->resize(stack->size() - 1);
            return 1;
        }

        cell_t N_StackPopArray(IPluginContext* ctx, const cell_t* params) { return StackArray(ctx, params, true); }
        cell_t N_StackTopArray(IPluginContext* ctx, const cell_t* params) { return StackArray(ctx, params, false); }

        cell_t N_StackEmpty(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* stack = StackOf(ctx, params[1]);
            return stack ? stack->size() == 0 : 1;
        }

        cell_t N_StackLength(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* stack = StackOf(ctx, params[1]);
            return stack ? static_cast<cell_t>(stack->size()) : 0;
        }

        cell_t N_StackBlockSize(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* stack = StackOf(ctx, params[1]);
            return stack ? static_cast<cell_t>(stack->blocksize) : 0;
        }

        // PopStackCell(stack, &value, block=0, asChar=false): false when empty.
        cell_t N_PopStackCell(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* stack = StackOf(ctx, params[1]);
            if (!stack || stack->size() == 0)
                return 0;
            cell_t out = 0;
            if (!ReadBlock(ctx, stack, stack->at(stack->size() - 1), params[0] >= 3 ? params[3] : 0, params[0] >= 4 && params[4], out))
                return 0;
            cell_t* addr;
            ctx->LocalToPhysAddr(params[2], &addr);
            *addr = out;
            stack->resize(stack->size() - 1);
            return 1;
        }

        cell_t N_PopStackString(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* stack = StackOf(ctx, params[1]);
            if (!stack || stack->size() == 0)
                return 0;
            return StackString(ctx, params, true);
        }

        cell_t N_PopStackArray(IPluginContext* ctx, const cell_t* params)
        {
            CellArray* stack = StackOf(ctx, params[1]);
            if (!stack || stack->size() == 0)
                return 0;
            return StackArray(ctx, params, true);
        }

        // ---- StringMap ----------------------------------------------------------

        struct MapEntry
        {
            enum Kind { Cell, Array, String } kind = Cell;
            cell_t cell = 0;
            std::vector<cell_t> array;
            std::string string;
        };

        struct StringMap : HandleObject
        {
            std::unordered_map<std::string, MapEntry> map;
        };

        struct Snapshot : HandleObject
        {
            std::vector<std::string> keys;
        };

        StringMap* MapOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<StringMap*>(ReadHandle(ctx, handle, Types().stringMap));
        }

        cell_t N_StringMap(IPluginContext* ctx, const cell_t*)
        {
            return NewHandle(ctx, Types().stringMap, new StringMap());
        }

        cell_t N_MapClone(IPluginContext* ctx, const cell_t* params)
        {
            StringMap* map = MapOf(ctx, params[1]);
            if (!map)
                return 0;
            auto* copy = new StringMap();
            copy->map = map->map;
            return NewHandle(ctx, Types().stringMap, copy);
        }

        // Where a Set* writes: nullptr when the key is taken and replace is false.
        MapEntry* SlotFor(StringMap* map, const char* key, bool replace)
        {
            auto [it, inserted] = map->map.try_emplace(key);
            if (!inserted && !replace)
                return nullptr;
            it->second = MapEntry{};
            return &it->second;
        }

        // SetValue(key, value, replace=true)
        cell_t N_MapSetValue(IPluginContext* ctx, const cell_t* params)
        {
            StringMap* map = MapOf(ctx, params[1]);
            if (!map)
                return 0;
            MapEntry* e = SlotFor(map, LocalString(ctx, params[2]), params[0] < 4 || params[4]);
            if (!e)
                return 0;
            e->kind = MapEntry::Cell;
            e->cell = params[3];
            return 1;
        }

        // SetArray(key, array, num_items, replace=true)
        cell_t N_MapSetArray(IPluginContext* ctx, const cell_t* params)
        {
            StringMap* map = MapOf(ctx, params[1]);
            if (!map)
                return 0;
            if (params[4] < 0)
                return ctx->ThrowNativeError("Invalid array size: %d", params[4]);
            cell_t* src;
            ctx->LocalToPhysAddr(params[3], &src);
            MapEntry* e = SlotFor(map, LocalString(ctx, params[2]), params[0] < 5 || params[5]);
            if (!e)
                return 0;
            e->kind = MapEntry::Array;
            e->array.assign(src, src + params[4]);
            return 1;
        }

        // SetString(key, value, replace=true)
        cell_t N_MapSetString(IPluginContext* ctx, const cell_t* params)
        {
            StringMap* map = MapOf(ctx, params[1]);
            if (!map)
                return 0;
            const std::string value = LocalString(ctx, params[3]);
            MapEntry* e = SlotFor(map, LocalString(ctx, params[2]), params[0] < 4 || params[4]);
            if (!e)
                return 0;
            e->kind = MapEntry::String;
            e->string = value;
            return 1;
        }

        const MapEntry* Find(StringMap* map, const char* key)
        {
            auto it = map->map.find(key);
            return it != map->map.end() ? &it->second : nullptr;
        }

        // GetValue(key, &value)
        cell_t N_MapGetValue(IPluginContext* ctx, const cell_t* params)
        {
            StringMap* map = MapOf(ctx, params[1]);
            if (!map)
                return 0;
            const MapEntry* e = Find(map, LocalString(ctx, params[2]));
            if (!e)
                return 0;

            cell_t value;
            if (e->kind == MapEntry::Cell)
                value = e->cell;
            // SourceMod's compatibility rule: a one-cell array reads as a value.
            else if (e->kind == MapEntry::Array && e->array.size() == 1)
                value = e->array[0];
            else
                return 0;

            cell_t* addr;
            ctx->LocalToPhysAddr(params[3], &addr);
            *addr = value;
            return 1;
        }

        // GetArray(key, array, max_size, &size=0)
        cell_t N_MapGetArray(IPluginContext* ctx, const cell_t* params)
        {
            StringMap* map = MapOf(ctx, params[1]);
            if (!map)
                return 0;
            if (params[4] < 0)
                return ctx->ThrowNativeError("Invalid array size: %d", params[4]);
            const MapEntry* e = Find(map, LocalString(ctx, params[2]));
            if (!e)
                return 0;

            cell_t* dest;
            ctx->LocalToPhysAddr(params[3], &dest);
            size_t count;
            if (e->kind == MapEntry::Cell)
            {
                count = params[4] > 0 ? 1 : 0;
                if (count)
                    dest[0] = e->cell;
            }
            else if (e->kind == MapEntry::Array)
            {
                count = std::min(e->array.size(), static_cast<size_t>(params[4]));
                memcpy(dest, e->array.data(), count * sizeof(cell_t));
            }
            else
                return 0;

            if (params[0] >= 5)
            {
                cell_t* size;
                if (ctx->LocalToPhysAddr(params[5], &size) == SP_ERROR_NONE)
                    *size = static_cast<cell_t>(count);
            }
            return 1;
        }

        // GetString(key, value, max_size, &size=0)
        cell_t N_MapGetString(IPluginContext* ctx, const cell_t* params)
        {
            StringMap* map = MapOf(ctx, params[1]);
            if (!map)
                return 0;
            if (params[4] < 0)
                return ctx->ThrowNativeError("Invalid buffer size: %d", params[4]);
            const MapEntry* e = Find(map, LocalString(ctx, params[2]));
            if (!e || e->kind != MapEntry::String)
                return 0;

            size_t written = 0;
            ctx->StringToLocalUTF8(params[3], params[4], e->string.c_str(), &written);
            if (params[0] >= 5)
            {
                cell_t* size;
                if (ctx->LocalToPhysAddr(params[5], &size) == SP_ERROR_NONE)
                    *size = static_cast<cell_t>(written);
            }
            return 1;
        }

        cell_t N_MapContainsKey(IPluginContext* ctx, const cell_t* params)
        {
            StringMap* map = MapOf(ctx, params[1]);
            return map && Find(map, LocalString(ctx, params[2]));
        }

        cell_t N_MapRemove(IPluginContext* ctx, const cell_t* params)
        {
            StringMap* map = MapOf(ctx, params[1]);
            return map && map->map.erase(LocalString(ctx, params[2])) > 0;
        }

        cell_t N_MapClear(IPluginContext* ctx, const cell_t* params)
        {
            if (StringMap* map = MapOf(ctx, params[1]))
                map->map.clear();
            return 0;
        }

        cell_t N_MapSize(IPluginContext* ctx, const cell_t* params)
        {
            StringMap* map = MapOf(ctx, params[1]);
            return map ? static_cast<cell_t>(map->map.size()) : 0;
        }

        cell_t N_MapSnapshot(IPluginContext* ctx, const cell_t* params)
        {
            StringMap* map = MapOf(ctx, params[1]);
            if (!map)
                return 0;
            auto* snap = new Snapshot();
            snap->keys.reserve(map->map.size());
            for (const auto& [key, value] : map->map)
                snap->keys.push_back(key);
            return NewHandle(ctx, Types().snapshot, snap);
        }

        Snapshot* SnapshotOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<Snapshot*>(ReadHandle(ctx, handle, Types().snapshot));
        }

        cell_t N_SnapshotLength(IPluginContext* ctx, const cell_t* params)
        {
            Snapshot* snap = SnapshotOf(ctx, params[1]);
            return snap ? static_cast<cell_t>(snap->keys.size()) : 0;
        }

        const std::string* SnapshotKey(IPluginContext* ctx, Snapshot* snap, cell_t index)
        {
            if (index < 0 || static_cast<size_t>(index) >= snap->keys.size())
            {
                ctx->ThrowNativeError("Invalid index %d", index);
                return nullptr;
            }
            return &snap->keys[index];
        }

        cell_t N_SnapshotKeyBufferSize(IPluginContext* ctx, const cell_t* params)
        {
            Snapshot* snap = SnapshotOf(ctx, params[1]);
            const std::string* key = snap ? SnapshotKey(ctx, snap, params[2]) : nullptr;
            return key ? static_cast<cell_t>(key->size() + 1) : 0;
        }

        cell_t N_SnapshotGetKey(IPluginContext* ctx, const cell_t* params)
        {
            Snapshot* snap = SnapshotOf(ctx, params[1]);
            const std::string* key = snap ? SnapshotKey(ctx, snap, params[2]) : nullptr;
            if (!key)
                return 0;
            size_t written = 0;
            ctx->StringToLocalUTF8(params[3], params[4], key->c_str(), &written);
            return static_cast<cell_t>(written);
        }

        // ---- DataPack -----------------------------------------------------------

        // CDataPack's element types, by number: what "got %d / expected %d" shows.
        enum PackType : cell_t
        {
            Pack_Raw,
            Pack_Cell,
            Pack_Float,
            Pack_String,
            Pack_Function,
            Pack_CellArray,
            Pack_FloatArray,
        };

        struct PackElement
        {
            PackType type;
            cell_t cell = 0;
            std::string string;
            std::vector<cell_t> array;
        };

        struct DataPack : HandleObject
        {
            std::vector<PackElement> elements;
            size_t position = 0;

            // A write replaces what is at the position unless it inserts.
            void Write(PackElement e, bool insert)
            {
                if (!insert && position < elements.size())
                    elements.erase(elements.begin() + position);
                elements.insert(elements.begin() + position, std::move(e));
                position++;
            }
        };

        DataPack* PackOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<DataPack*>(ReadHandle(ctx, handle, Types().dataPack));
        }

        // The element a Read* takes, or a script error.
        const PackElement* ReadElement(IPluginContext* ctx, DataPack* pack, PackType type)
        {
            if (pack->position >= pack->elements.size())
            {
                ctx->ThrowNativeError("Data pack operation is out of bounds.");
                return nullptr;
            }
            const PackElement& e = pack->elements[pack->position];
            if (e.type != type)
            {
                ctx->ThrowNativeError("Invalid data pack type (got %d / expected %d).", static_cast<int>(e.type), static_cast<int>(type));
                return nullptr;
            }
            pack->position++;
            return &e;
        }

        bool InsertFlag(const cell_t* params, int at)
        {
            return params[0] >= at && params[at];
        }

        cell_t N_DataPack(IPluginContext* ctx, const cell_t*)
        {
            return NewHandle(ctx, Types().dataPack, new DataPack());
        }

        cell_t N_PackWriteCell(IPluginContext* ctx, const cell_t* params)
        {
            if (DataPack* pack = PackOf(ctx, params[1]))
                pack->Write({ Pack_Cell, params[2] }, InsertFlag(params, 3));
            return 0;
        }

        cell_t N_PackWriteFloat(IPluginContext* ctx, const cell_t* params)
        {
            if (DataPack* pack = PackOf(ctx, params[1]))
                pack->Write({ Pack_Float, params[2] }, InsertFlag(params, 3));
            return 0;
        }

        cell_t N_PackWriteString(IPluginContext* ctx, const cell_t* params)
        {
            if (DataPack* pack = PackOf(ctx, params[1]))
            {
                PackElement e{ Pack_String };
                e.string = LocalString(ctx, params[2]);
                pack->Write(std::move(e), InsertFlag(params, 3));
            }
            return 0;
        }

        cell_t N_PackWriteFunction(IPluginContext* ctx, const cell_t* params)
        {
            DataPack* pack = PackOf(ctx, params[1]);
            if (!pack)
                return 0;
            if (!ctx->GetFunctionById(params[2]))
                return ctx->ThrowNativeError("Invalid function id (%X)", params[2]);
            pack->Write({ Pack_Function, params[2] }, InsertFlag(params, 3));
            return 0;
        }

        cell_t PackWriteArray(IPluginContext* ctx, const cell_t* params, PackType type)
        {
            DataPack* pack = PackOf(ctx, params[1]);
            if (!pack)
                return 0;
            if (params[3] < 0)
                return ctx->ThrowNativeError("Invalid array size: %d", params[3]);
            cell_t* src;
            ctx->LocalToPhysAddr(params[2], &src);
            PackElement e{ type };
            e.array.assign(src, src + params[3]);
            pack->Write(std::move(e), InsertFlag(params, 4));
            return 0;
        }

        cell_t N_PackWriteCellArray(IPluginContext* ctx, const cell_t* params) { return PackWriteArray(ctx, params, Pack_CellArray); }
        cell_t N_PackWriteFloatArray(IPluginContext* ctx, const cell_t* params) { return PackWriteArray(ctx, params, Pack_FloatArray); }

        cell_t N_PackReadCell(IPluginContext* ctx, const cell_t* params)
        {
            DataPack* pack = PackOf(ctx, params[1]);
            const PackElement* e = pack ? ReadElement(ctx, pack, Pack_Cell) : nullptr;
            return e ? e->cell : 0;
        }

        cell_t N_PackReadFloat(IPluginContext* ctx, const cell_t* params)
        {
            DataPack* pack = PackOf(ctx, params[1]);
            const PackElement* e = pack ? ReadElement(ctx, pack, Pack_Float) : nullptr;
            return e ? e->cell : 0;
        }

        cell_t N_PackReadFunction(IPluginContext* ctx, const cell_t* params)
        {
            DataPack* pack = PackOf(ctx, params[1]);
            const PackElement* e = pack ? ReadElement(ctx, pack, Pack_Function) : nullptr;
            return e ? e->cell : 0;
        }

        cell_t N_PackReadString(IPluginContext* ctx, const cell_t* params)
        {
            DataPack* pack = PackOf(ctx, params[1]);
            const PackElement* e = pack ? ReadElement(ctx, pack, Pack_String) : nullptr;
            if (e)
                ctx->StringToLocalUTF8(params[2], params[3], e->string.c_str(), nullptr);
            return 0;
        }

        cell_t PackReadArray(IPluginContext* ctx, const cell_t* params, PackType type)
        {
            DataPack* pack = PackOf(ctx, params[1]);
            if (!pack)
                return 0;
            if (pack->position >= pack->elements.size())
                return ctx->ThrowNativeError("Data pack operation is out of bounds.");
            const PackElement& peek = pack->elements[pack->position];
            if (peek.type != type)
                return ctx->ThrowNativeError("Invalid data pack type (got %d / expected %d).", static_cast<int>(peek.type), static_cast<int>(type));
            if (static_cast<size_t>(std::max(params[3], 0)) < peek.array.size())
                return ctx->ThrowNativeError("Input buffer too small (needed %d, got %d).", static_cast<int>(peek.array.size()), params[3]);

            const PackElement* e = ReadElement(ctx, pack, type);
            cell_t* dest;
            ctx->LocalToPhysAddr(params[2], &dest);
            memcpy(dest, e->array.data(), e->array.size() * sizeof(cell_t));
            return 0;
        }

        cell_t N_PackReadCellArray(IPluginContext* ctx, const cell_t* params) { return PackReadArray(ctx, params, Pack_CellArray); }
        cell_t N_PackReadFloatArray(IPluginContext* ctx, const cell_t* params) { return PackReadArray(ctx, params, Pack_FloatArray); }

        // Reset(clear=false)
        cell_t N_PackReset(IPluginContext* ctx, const cell_t* params)
        {
            DataPack* pack = PackOf(ctx, params[1]);
            if (!pack)
                return 0;
            if (params[0] >= 2 && params[2])
                pack->elements.clear();
            pack->position = 0;
            return 0;
        }

        cell_t N_PackIsReadable(IPluginContext* ctx, const cell_t* params)
        {
            DataPack* pack = PackOf(ctx, params[1]);
            return pack && pack->position < pack->elements.size();
        }

        cell_t N_PackGetPosition(IPluginContext* ctx, const cell_t* params)
        {
            DataPack* pack = PackOf(ctx, params[1]);
            return pack ? static_cast<cell_t>(pack->position) : 0;
        }

        cell_t N_PackSetPosition(IPluginContext* ctx, const cell_t* params)
        {
            DataPack* pack = PackOf(ctx, params[1]);
            if (!pack)
                return 0;
            if (params[2] < 0 || static_cast<size_t>(params[2]) > pack->elements.size())
                return ctx->ThrowNativeError("Invalid data pack position, %d is out of bounds (%d)", params[2], static_cast<int>(pack->elements.size()));
            pack->position = static_cast<size_t>(params[2]);
            return 0;
        }

        const sp_nativeinfo_t g_adtNatives[] = {
            // adt_array.inc
            { "ArrayList.ArrayList", N_ArrayList },
            { "ArrayList.Clear", N_ArrayClear },
            { "ArrayList.Clone", N_ArrayClone },
            { "ArrayList.Resize", N_ArrayResize },
            { "ArrayList.Push", N_ArrayPush },
            { "ArrayList.PushString", N_ArrayPushString },
            { "ArrayList.PushArray", N_ArrayPushArray },
            { "ArrayList.Get", N_ArrayGet },
            { "ArrayList.GetString", N_ArrayGetString },
            { "ArrayList.GetArray", N_ArrayGetArray },
            { "ArrayList.Set", N_ArraySet },
            { "ArrayList.SetString", N_ArraySetString },
            { "ArrayList.SetArray", N_ArraySetArray },
            { "ArrayList.ShiftUp", N_ArrayShiftUp },
            { "ArrayList.Erase", N_ArrayErase },
            { "ArrayList.SwapAt", N_ArraySwapAt },
            { "ArrayList.FindString", N_ArrayFindString },
            { "ArrayList.FindValue", N_ArrayFindValue },
            { "ArrayList.Sort", N_ArraySort },
            { "ArrayList.SortCustom", N_ArraySortCustom },
            { "ArrayList.Length.get", N_ArrayLength },
            { "ArrayList.BlockSize.get", N_ArrayBlockSize },
            { "CreateArray", N_ArrayList },
            { "ClearArray", N_ArrayClear },
            { "CloneArray", N_ArrayClone },
            { "ResizeArray", N_ArrayResize },
            { "GetArraySize", N_ArrayLength },
            { "PushArrayCell", N_ArrayPush },
            { "PushArrayString", N_ArrayPushString },
            { "PushArrayArray", N_ArrayPushArray },
            { "GetArrayCell", N_ArrayGet },
            { "GetArrayString", N_ArrayGetString },
            { "GetArrayArray", N_ArrayGetArray },
            { "SetArrayCell", N_ArraySet },
            { "SetArrayString", N_ArraySetString },
            { "SetArrayArray", N_ArraySetArray },
            { "ShiftArrayUp", N_ArrayShiftUp },
            { "RemoveFromArray", N_ArrayErase },
            { "SwapArrayItems", N_ArraySwapAt },
            { "FindStringInArray", N_ArrayFindString },
            { "FindValueInArray", N_ArrayFindValue },
            { "GetArrayBlockSize", N_ArrayBlockSize },
            // sorting.inc
            { "SortADTArray", N_ArraySort },
            { "SortADTArrayCustom", N_ArraySortCustom },
            // adt_stack.inc
            { "ArrayStack.ArrayStack", N_ArrayStack },
            { "ArrayStack.Clear", N_StackClear },
            { "ArrayStack.Clone", N_StackClone },
            { "ArrayStack.Push", N_StackPush },
            { "ArrayStack.PushString", N_StackPushString },
            { "ArrayStack.PushArray", N_StackPushArray },
            { "ArrayStack.Pop", N_StackPop },
            { "ArrayStack.Top", N_StackTop },
            { "ArrayStack.PopString", N_StackPopString },
            { "ArrayStack.TopString", N_StackTopString },
            { "ArrayStack.PopArray", N_StackPopArray },
            { "ArrayStack.TopArray", N_StackTopArray },
            { "ArrayStack.Empty.get", N_StackEmpty },
            { "ArrayStack.Length.get", N_StackLength },
            { "ArrayStack.BlockSize.get", N_StackBlockSize },
            { "CreateStack", N_ArrayStack },
            { "PushStackCell", N_StackPush },
            { "PushStackString", N_StackPushString },
            { "PushStackArray", N_StackPushArray },
            { "PopStackCell", N_PopStackCell },
            { "PopStackString", N_PopStackString },
            { "PopStackArray", N_PopStackArray },
            { "IsStackEmpty", N_StackEmpty },
            { "GetStackBlockSize", N_StackBlockSize },
            // adt_trie.inc
            { "StringMap.StringMap", N_StringMap },
            { "StringMap.Clone", N_MapClone },
            { "StringMap.SetValue", N_MapSetValue },
            { "StringMap.SetArray", N_MapSetArray },
            { "StringMap.SetString", N_MapSetString },
            { "StringMap.GetValue", N_MapGetValue },
            { "StringMap.GetArray", N_MapGetArray },
            { "StringMap.GetString", N_MapGetString },
            { "StringMap.ContainsKey", N_MapContainsKey },
            { "StringMap.Remove", N_MapRemove },
            { "StringMap.Clear", N_MapClear },
            { "StringMap.Snapshot", N_MapSnapshot },
            { "StringMap.Size.get", N_MapSize },
            { "StringMapSnapshot.Length.get", N_SnapshotLength },
            { "StringMapSnapshot.KeyBufferSize", N_SnapshotKeyBufferSize },
            { "StringMapSnapshot.GetKey", N_SnapshotGetKey },
            { "CreateTrie", N_StringMap },
            { "SetTrieValue", N_MapSetValue },
            { "SetTrieArray", N_MapSetArray },
            { "SetTrieString", N_MapSetString },
            { "GetTrieValue", N_MapGetValue },
            { "GetTrieArray", N_MapGetArray },
            { "GetTrieString", N_MapGetString },
            { "RemoveFromTrie", N_MapRemove },
            { "ClearTrie", N_MapClear },
            { "GetTrieSize", N_MapSize },
            { "CreateTrieSnapshot", N_MapSnapshot },
            { "TrieSnapshotLength", N_SnapshotLength },
            { "TrieSnapshotKeyBufferSize", N_SnapshotKeyBufferSize },
            { "GetTrieSnapshotKey", N_SnapshotGetKey },
            // datapack.inc
            { "DataPack.DataPack", N_DataPack },
            { "DataPack.WriteCell", N_PackWriteCell },
            { "DataPack.WriteFloat", N_PackWriteFloat },
            { "DataPack.WriteString", N_PackWriteString },
            { "DataPack.WriteFunction", N_PackWriteFunction },
            { "DataPack.WriteCellArray", N_PackWriteCellArray },
            { "DataPack.WriteFloatArray", N_PackWriteFloatArray },
            { "DataPack.ReadCell", N_PackReadCell },
            { "DataPack.ReadFloat", N_PackReadFloat },
            { "DataPack.ReadString", N_PackReadString },
            { "DataPack.ReadFunction", N_PackReadFunction },
            { "DataPack.ReadCellArray", N_PackReadCellArray },
            { "DataPack.ReadFloatArray", N_PackReadFloatArray },
            { "DataPack.Reset", N_PackReset },
            { "DataPack.IsReadable", N_PackIsReadable },
            { "DataPack.Position.get", N_PackGetPosition },
            { "DataPack.Position.set", N_PackSetPosition },
            { "CreateDataPack", N_DataPack },
            { "WritePackCell", N_PackWriteCell },
            { "WritePackFloat", N_PackWriteFloat },
            { "WritePackString", N_PackWriteString },
            { "WritePackFunction", N_PackWriteFunction },
            { "ReadPackCell", N_PackReadCell },
            { "ReadPackFloat", N_PackReadFloat },
            { "ReadPackString", N_PackReadString },
            { "ReadPackFunction", N_PackReadFunction },
            { "ResetPack", N_PackReset },
            { "GetPackPosition", N_PackGetPosition },
            { "SetPackPosition", N_PackSetPosition },
            { "IsPackReadable", N_PackIsReadable },
            { nullptr, nullptr },
        };
    }

    cell_t NewCellList(const std::vector<cell_t>& items, PluginId owner)
    {
        auto* array = new CellArray(1);
        array->data = items;
        const cell_t h = Handles().Create(Types().arrayList, array, owner);
        if (!h)
            delete array;
        return h;
    }

    cell_t FillStringList(IPluginContext* ctx, cell_t handle, const std::vector<std::string>& items, bool clear, cell_t blocksize, PluginId owner)
    {
        CellArray* array = nullptr;
        cell_t result = handle;
        if (handle != kInvalidHandle)
        {
            if (!(array = ArrayOf(ctx, handle)))
                return 0;
            if (clear)
                array->data.clear();
        }
        else
        {
            array = new CellArray(static_cast<size_t>(blocksize > 0 ? blocksize : 1));
            if (!(result = Handles().Create(Types().arrayList, array, owner)))
            {
                delete array;
                return ctx->ThrowNativeError("Handle limit reached");
            }
        }
        for (const std::string& item : items)
            WriteStringBlock(array, array->push(), 0, item.c_str(), array->blocksize * sizeof(cell_t));
        return result;
    }

    bool ReadStringList(IPluginContext* ctx, cell_t handle, std::vector<std::string>& items)
    {
        CellArray* array = ArrayOf(ctx, handle);
        if (!array)
            return false;
        const size_t bytes = array->blocksize * sizeof(cell_t);
        for (size_t i = 0; i < array->size(); i++)
        {
            const char* text = reinterpret_cast<const char*>(array->at(i));
            items.emplace_back(text, strnlen(text, bytes));
        }
        return true;
    }

    const sp_nativeinfo_t* GetAdtNatives()
    {
        return g_adtNatives;
    }
}
