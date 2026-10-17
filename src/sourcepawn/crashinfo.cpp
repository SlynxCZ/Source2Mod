/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * Crash-time view of the SourcePawn VM; see crashinfo.h. The plugin table
 * block follows Accelerator's SerializePluginContexts / OnPluginLoaded /
 * OnPluginUnloaded (Asher Baker, github.com/asherkin/accelerator): the same
 * header and tail magic, rebuilt and re-registered on every load and unload.
 *
 * Project: Source2Mod
 */
#include "crashinfo.h"
#include "scriptplugin.h"

#include "core/crashhandler.h"

#include <base-runtime.h>
#include <environment.h>
#include <smx-image.h>
#include <stack-frames.h>
#include <x64/frames-x64.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace sourcepawn::crashinfo
{
    State g_state = { kStateMagic, 0, 0, 0, {} };

    namespace
    {
        struct Function
        {
            const char* name;
            uint32_t start;
            uint32_t end;
        };

        // Written while the server runs, read by the crash callback. A slot
        // is published by storing runtime last and retired by clearing it
        // first. The dump reader builds the same thing from the block.
        struct Slot
        {
            volatile uint64_t runtime;
            bool v2;
            char file[128];
            // v2: by method index (the JIT frame id, and funcid >> 1).
            // v1: the RTTI methods when the binary has them, by code offset.
            const Function* methods;
            uint32_t numMethods;
            // By public index (v1 funcid >> 1).
            const Function* publics;
            uint32_t numPublics;
            const char* const* natives;
            uint32_t numNatives;
        };

        constexpr size_t kMaxSlots = 512;
        Slot g_slots[kMaxSlots];

        // Scan counts, kept off the (alternate) signal stack.
        uint32_t g_contextHits[kMaxSlots];

        // Accelerator's magic numbers; version 1 is its layout (context,
        // file, publics), version 2 ours:
        //   u64 header magic, u32 version, u32 size, u32 count,
        //   count x { u32 size, u64 context, u8 v2, char file[],
        //             u32 n, n x { u32 start, u32 end, char name[] }   methods
        //             u32 n, n x { u32 code_offs, char name[] }        publics
        //             u32 n, n x { char name[] } }                     natives
        //   u64 tail magic
        constexpr uint64_t kBlockHeaderMagic = 103582791429521979ULL;
        constexpr uint64_t kBlockTailMagic = 76561197987819599ULL;
        constexpr uint32_t kBlockVersion = 2;

        unsigned char* g_block = nullptr;
        bool g_stateRegistered = false;

        // What Describe works from: live memory or a dump.
        struct Model
        {
            const Slot* slots;
            size_t numSlots;
            uint64_t topRuntime;      // the runtime the innermost invoke runs in
            uint64_t exitFp;
            const CallEntry* calls;
            uint32_t depth;
        };

        const Slot* FindSlot(const Model& m, uint64_t runtime)
        {
            if (!runtime)
                return nullptr;
            for (size_t i = 0; i < m.numSlots; ++i)
            {
                if (m.slots[i].runtime == runtime)
                    return &m.slots[i];
            }
            return nullptr;
        }

        const char* FunctionByFuncId(const Slot* slot, uint32_t funcid)
        {
            if (!slot || !(funcid & 1))
                return nullptr;
            const uint32_t index = funcid >> 1;
            if (slot->v2)
                return index < slot->numMethods ? slot->methods[index].name : nullptr;
            return index < slot->numPublics ? slot->publics[index].name : nullptr;
        }

        // A JIT frame id: the method index in v2, the function's code offset
        // in v1. A v1 binary without RTTI falls back to the nearest public
        // below, which is what Throttle reports for SourceMod.
        const char* FunctionByFrameId(const Slot* slot, uint32_t id, bool* nearest)
        {
            *nearest = false;
            if (!slot)
                return nullptr;
            if (slot->v2)
                return id < slot->numMethods ? slot->methods[id].name : nullptr;

            for (uint32_t i = 0; i < slot->numMethods; ++i)
            {
                const Function& fn = slot->methods[i];
                if (id >= fn.start && id < fn.end)
                    return fn.name;
            }

            const Function* best = nullptr;
            for (uint32_t i = 0; i < slot->numPublics; ++i)
            {
                const Function& fn = slot->publics[i];
                if (fn.name && fn.start <= id && (!best || fn.start > best->start))
                    best = &fn;
            }
            if (!best)
                return nullptr;
            *nearest = best->start != id;
            return best->name;
        }

        class Appender
        {
        public:
            Appender(char* out, size_t maxlen) : m_out(out), m_max(maxlen)
            {
                if (m_max)
                    m_out[0] = '\0';
            }

            Appender& operator()(const char* str)
            {
                if (!str || !*str)
                    str = "<unknown>";
                while (*str && m_len + 1 < m_max)
                    m_out[m_len++] = *str++;
                if (m_max)
                    m_out[m_len] = '\0';
                return *this;
            }

            Appender& Dec(uint64_t value)
            {
                char buf[24];
                char* p = buf + sizeof(buf);
                *--p = '\0';
                do
                {
                    *--p = char('0' + value % 10);
                    value /= 10;
                } while (value);
                return (*this)(p);
            }

            size_t Length() const { return m_len; }

        private:
            char* m_out;
            size_t m_max;
            size_t m_len = 0;
        };

        bool ReadStack(const StackCopy* stack, uint64_t addr, void* out, size_t len)
        {
            if (addr < stack->base || addr + len < addr || addr + len > stack->base + stack->size)
                return false;
            memcpy(out, stack->bytes + (addr - stack->base), len);
            return true;
        }

        const char* SlotFile(const Slot* slot)
        {
            return slot ? slot->file : nullptr;
        }

        struct ScriptFrame
        {
            const Slot* slot;
            const char* name;
            bool nearest;
        };

        constexpr size_t kMaxFrames = 32;

        size_t DescribeModel(char* out, size_t maxlen, const Model& m, const StackCopy* stack)
        {
            Appender a(out, maxlen);
            a("-------- SOURCEPAWN BEGIN --------\n");

            // The native being called and the script frames under it, from
            // the JIT's own frames. exit_fp stays behind when a native
            // returns, so it counts only when it lies on the live part of the
            // crashed stack.
            const char* nativeName = nullptr;
            bool inNative = false;
            bool inHelper = false;
            ScriptFrame frames[kMaxFrames];
            size_t numFrames = 0;

            if (m.topRuntime && m.exitFp && stack && m.exitFp >= stack->sp && m.exitFp < stack->base + stack->size)
            {
                const Slot* slot = FindSlot(m, m.topRuntime);
                uint64_t fp = m.exitFp;
                for (int i = 0; i < 256 && numFrames < kMaxFrames; ++i)
                {
                    sp::FrameLayout layout;
                    const uint64_t at = fp + sp::FrameLayout::kOffsetFromFp * int64_t(sizeof(intptr_t));
                    if (!ReadStack(stack, at, &layout, sizeof(layout)))
                        break;

                    const auto type = sp::JitFrameType(layout.frame_type());
                    const uintptr_t id = uint32_t(layout.function_id());
                    if (type == sp::JitFrameType::Entry)
                        break;

                    if (type == sp::JitFrameType::Exit)
                    {
                        if (i == 0 && sp::GetExitFrameType(id) == sp::ExitFrameType::Native)
                        {
                            inNative = true;
                            const uintptr_t index = sp::GetExitFramePayload(id);
                            if (slot && index < slot->numNatives)
                                nativeName = slot->natives[index];
                        }
                        else if (i == 0)
                            inHelper = true;
                    }
                    else if (type == sp::JitFrameType::Scripted || type == sp::JitFrameType::Uninitialized)
                    {
                        ScriptFrame& frame = frames[numFrames++];
                        frame.slot = slot;
                        frame.name = FunctionByFrameId(slot, uint32_t(id), &frame.nearest);
                    }

                    const uint64_t prev = reinterpret_cast<uint64_t>(layout.prev_fp);
                    if (prev <= fp)
                        break;
                    fp = prev;
                }
            }

            const uint32_t numCalls = m.depth < kMaxCallDepth ? m.depth : kMaxCallDepth;

            // As Throttle does with Accelerator's block: a plugin context
            // pointer on the crashed stack is a plugin the thread was in.
            bool scanned = false;
            const Slot* firstOnStack = nullptr;
            if (stack && stack->sp >= stack->base && stack->sp < stack->base + stack->size)
            {
                scanned = true;
                memset(g_contextHits, 0, sizeof(g_contextHits));
                const uint64_t end = stack->base + stack->size;
                for (uint64_t addr = (stack->sp + 7) & ~uint64_t(7); addr + 8 <= end; addr += 8)
                {
                    uint64_t value;
                    memcpy(&value, stack->bytes + (addr - stack->base), sizeof(value));
                    if (!value)
                        continue;
                    for (size_t i = 0; i < m.numSlots; ++i)
                    {
                        if (m.slots[i].runtime == value)
                        {
                            // The innermost hit (lowest address) is the likeliest.
                            if (!g_contextHits[i]++ && !firstOnStack)
                                firstOnStack = &m.slots[i];
                            break;
                        }
                    }
                }
            }

            a("Blaming: ");
            if (numFrames)
            {
                a(SlotFile(frames[0].slot))("::")(frames[0].name);
                if (inNative)
                    a(" (in native ")(nativeName)(")");
            }
            else if (numCalls)
            {
                const Slot* slot = FindSlot(m, m.calls[numCalls - 1].runtime);
                a(SlotFile(slot))("::")(FunctionByFuncId(slot, m.calls[numCalls - 1].funcid));
            }
            else if (firstOnStack)
                a(firstOnStack->file)(" (its context is on the crashed stack)");
            else
                a("<none> (no SourcePawn code was running)");
            a("\n");

            if (inNative)
                a("Native: ")(nativeName)("\n");
            else if (inHelper)
                a("Native: <VM helper>\n");

            if (numFrames)
            {
                a("Script stack (innermost first):\n");
                for (size_t i = 0; i < numFrames; ++i)
                {
                    a("  [").Dec(i)("] ")(SlotFile(frames[i].slot))("::")(frames[i].name);
                    if (frames[i].nearest)
                        a(" (nearest public)");
                    a("\n");
                }
            }

            if (m.depth)
            {
                a("Script calls in progress (innermost first):\n");
                if (m.depth > kMaxCallDepth)
                    a("  (").Dec(m.depth - kMaxCallDepth)(" deeper calls not recorded)\n");
                for (uint32_t i = numCalls; i-- > 0;)
                {
                    const Slot* slot = FindSlot(m, m.calls[i].runtime);
                    a("  [").Dec(numCalls - 1 - i)("] ")(SlotFile(slot))("::");
                    if (const char* name = FunctionByFuncId(slot, m.calls[i].funcid))
                        a(name);
                    else
                        a("<function ").Dec(m.calls[i].funcid)(">");
                    a("\n");
                }
            }

            if (scanned)
            {
                bool any = false;
                for (size_t i = 0; i < m.numSlots; ++i)
                {
                    if (!g_contextHits[i] || !m.slots[i].runtime)
                        continue;
                    if (!any)
                        a("Plugin contexts on the crashed stack:\n");
                    any = true;
                    a("  ")(m.slots[i].file)(" (").Dec(g_contextHits[i])(")\n");
                }
                if (!any)
                    a("Plugin contexts on the crashed stack: none\n");
            }
            else
                a("Plugin contexts on the crashed stack: <stack not available>\n");

            a("-------- SOURCEPAWN END --------\n");
            return a.Length();
        }

        // ---- the block --------------------------------------------------------

        class Writer
        {
        public:
            explicit Writer(unsigned char* out) : m_out(out) {}

            void U8(uint8_t v) { Raw(&v, sizeof(v)); }
            void U32(uint32_t v) { Raw(&v, sizeof(v)); }
            void U64(uint64_t v) { Raw(&v, sizeof(v)); }
            void Str(const char* s)
            {
                if (!s)
                    s = "";
                Raw(s, strlen(s) + 1);
            }
            size_t Size() const { return m_size; }

        private:
            void Raw(const void* p, size_t n)
            {
                if (m_out)
                    memcpy(m_out + m_size, p, n);
                m_size += n;
            }

            unsigned char* m_out;
            size_t m_size = 0;
        };

        void WriteBlock(Writer& w)
        {
            uint32_t count = 0;
            for (const Slot& slot : g_slots)
                count += slot.runtime ? 1 : 0;

            w.U64(kBlockHeaderMagic);
            w.U32(kBlockVersion);
            w.U32(0); // size, patched by SerializeBlock()
            w.U32(count);

            for (const Slot& slot : g_slots)
            {
                if (!slot.runtime)
                    continue;

                // Once to measure the entry, once to write it with its size.
                Writer measure(nullptr);
                for (int pass = 0; pass < 2; ++pass)
                {
                    Writer& out = pass ? w : measure;
                    out.U32(pass ? uint32_t(measure.Size()) : 0);
                    out.U64(slot.runtime);
                    out.U8(slot.v2);
                    out.Str(slot.file);
                    out.U32(slot.numMethods);
                    for (uint32_t i = 0; i < slot.numMethods; ++i)
                    {
                        out.U32(slot.methods[i].start);
                        out.U32(slot.methods[i].end);
                        out.Str(slot.methods[i].name);
                    }
                    out.U32(slot.numPublics);
                    for (uint32_t i = 0; i < slot.numPublics; ++i)
                    {
                        out.U32(slot.publics[i].start);
                        out.Str(slot.publics[i].name);
                    }
                    out.U32(slot.numNatives);
                    for (uint32_t i = 0; i < slot.numNatives; ++i)
                        out.Str(slot.natives[i]);
                }
            }

            w.U64(kBlockTailMagic);
        }

        // Rebuilt on every load and unload, like Accelerator's.
        void SerializeBlock()
        {
            Writer measure(nullptr);
            WriteBlock(measure);
            const uint32_t size = uint32_t(measure.Size());

            auto* block = static_cast<unsigned char*>(malloc(size));
            if (!block)
                return;
            Writer w(block);
            WriteBlock(w);
            memcpy(block + sizeof(uint64_t) + sizeof(uint32_t), &size, sizeof(size));

            crashhandler::RegisterAppMemory(block, size);
            if (g_block)
            {
                crashhandler::UnregisterAppMemory(g_block);
                free(g_block);
            }
            g_block = block;
        }

        // ---- reading a dump ---------------------------------------------------

        bool ReadDump(const DumpRegion* regions, size_t count, uint64_t addr, void* out, size_t len)
        {
            for (size_t i = 0; i < count; ++i)
            {
                const DumpRegion& r = regions[i];
                if (r.bytes && addr >= r.base && addr + len >= addr && addr + len <= r.base + r.size)
                {
                    memcpy(out, r.bytes + (addr - r.base), len);
                    return true;
                }
            }
            return false;
        }

        class Reader
        {
        public:
            Reader(const uint8_t* p, size_t size) : m_p(p), m_end(p + size) {}

            bool U8(uint8_t* v) { return Raw(v, sizeof(*v)); }
            bool U32(uint32_t* v) { return Raw(v, sizeof(*v)); }
            bool U64(uint64_t* v) { return Raw(v, sizeof(*v)); }
            bool Str(const char** s)
            {
                const void* nul = memchr(m_p, '\0', size_t(m_end - m_p));
                if (!nul)
                    return false;
                *s = reinterpret_cast<const char*>(m_p);
                m_p = static_cast<const uint8_t*>(nul) + 1;
                return true;
            }
            size_t Left() const { return size_t(m_end - m_p); }

        private:
            bool Raw(void* out, size_t n)
            {
                if (size_t(m_end - m_p) < n)
                    return false;
                memcpy(out, m_p, n);
                m_p += n;
                return true;
            }

            const uint8_t* m_p;
            const uint8_t* m_end;
        };
    }

    void Track(ScriptPlugin* plugin)
    {
        auto* runtime = static_cast<sp::BaseRuntime*>(plugin->Runtime());

        if (!g_stateRegistered)
        {
            // The dump gets the call record and the VM's exit frame pointer.
            if (sp::Environment* env = sp::Environment::get())
            {
                g_state.exitFpAddress = reinterpret_cast<uintptr_t>(env->addressOfExit());
                crashhandler::RegisterAppMemory(env->addressOfExit(), sizeof(intptr_t));
            }
            crashhandler::RegisterAppMemory(&g_state, sizeof(g_state));
            g_stateRegistered = true;
        }

        for (Slot& slot : g_slots)
        {
            if (slot.runtime)
                continue;

            slot.v2 = runtime->AsV2() != nullptr;
            strncpy(slot.file, plugin->FileName().c_str(), sizeof(slot.file) - 1);
            slot.file[sizeof(slot.file) - 1] = '\0';

            sp::SmxImage* image = runtime->image();
            const sp::smx_rtti_table_header* rtti = image ? image->rtti_methods() : nullptr;
            slot.numMethods = rtti ? rtti->row_count : 0;
            auto* methods = slot.numMethods ? new Function[slot.numMethods] : nullptr;
            for (uint32_t i = 0; i < slot.numMethods; ++i)
            {
                const sp::smx_rtti_method* method = image->GetMethod(i);
                if (method)
                    methods[i] = { image->names() + method->name, method->pcode_start, method->pcode_end };
                else
                    methods[i] = { nullptr, 0, 0 };
            }
            slot.methods = methods;

            slot.numPublics = runtime->GetPublicsNum();
            auto* publics = slot.numPublics ? new Function[slot.numPublics] : nullptr;
            for (uint32_t i = 0; i < slot.numPublics; ++i)
            {
                sp_public_t* pub = nullptr;
                if (runtime->GetPublicByIndex(i, &pub) == SP_ERROR_NONE && pub)
                    publics[i] = { pub->name, pub->code_offs, pub->code_offs };
                else
                    publics[i] = { nullptr, 0, 0 };
            }
            slot.publics = publics;

            slot.numNatives = runtime->GetNativesNum();
            auto* natives = slot.numNatives ? new const char*[slot.numNatives] : nullptr;
            for (uint32_t i = 0; i < slot.numNatives; ++i)
            {
                const sp_native_t* native = runtime->GetNative(i);
                natives[i] = native ? native->name : nullptr;
            }
            slot.natives = natives;

            std::atomic_thread_fence(std::memory_order_release);
            slot.runtime = reinterpret_cast<uintptr_t>(runtime);
            break;
        }

        SerializeBlock();
    }

    void Untrack(ScriptPlugin* plugin)
    {
        const uint64_t runtime = reinterpret_cast<uintptr_t>(plugin->Runtime());
        for (Slot& slot : g_slots)
        {
            if (slot.runtime != runtime)
                continue;

            slot.runtime = 0;
            std::atomic_thread_fence(std::memory_order_release);
            delete[] slot.methods;
            delete[] slot.publics;
            delete[] slot.natives;
            slot.methods = nullptr;
            slot.publics = nullptr;
            slot.natives = nullptr;
            slot.numMethods = slot.numPublics = slot.numNatives = 0;
            break;
        }

        SerializeBlock();
    }

    void Shutdown()
    {
        if (g_block)
        {
            crashhandler::UnregisterAppMemory(g_block);
            free(g_block);
            g_block = nullptr;
        }
        if (g_stateRegistered)
        {
            if (g_state.exitFpAddress)
                crashhandler::UnregisterAppMemory(reinterpret_cast<void*>(uintptr_t(g_state.exitFpAddress)));
            crashhandler::UnregisterAppMemory(&g_state);
            g_state.exitFpAddress = 0;
            g_stateRegistered = false;
        }
    }

    size_t Describe(char* out, size_t maxlen, const StackCopy* stack)
    {
        sp::Environment* env = sp::Environment::get();
        sp::InvokeFrame* top = env ? env->top() : nullptr;

        Model m;
        m.slots = g_slots;
        m.numSlots = kMaxSlots;
        m.topRuntime = top ? reinterpret_cast<uintptr_t>(top->cx()) : 0;
        m.exitFp = env ? reinterpret_cast<uintptr_t>(env->exit_fp()) : 0;
        m.calls = g_state.calls;
        m.depth = g_state.depth;
        return DescribeModel(out, maxlen, m, stack);
    }

    size_t DescribeFromDump(char* out, size_t maxlen, const DumpRegion* regions, size_t count, const StackCopy* stack)
    {
        if (maxlen)
            out[0] = '\0';

        const DumpRegion* blockRegion = nullptr;
        State state{};
        bool haveState = false;
        for (size_t i = 0; i < count; ++i)
        {
            const DumpRegion& r = regions[i];
            uint64_t magic = 0;
            if (!r.bytes || r.size < sizeof(magic))
                continue;
            memcpy(&magic, r.bytes, sizeof(magic));
            if (magic == kBlockHeaderMagic && !blockRegion)
                blockRegion = &r;
            else if (magic == kStateMagic && r.size >= sizeof(State) && !haveState)
            {
                memcpy(&state, r.bytes, sizeof(State));
                haveState = true;
            }
        }
        if (!blockRegion)
            return 0;

        Reader in(blockRegion->bytes, blockRegion->size);
        uint64_t magic;
        uint32_t version, size, numPlugins;
        if (!in.U64(&magic) || !in.U32(&version) || !in.U32(&size) || !in.U32(&numPlugins) || version != kBlockVersion)
            return 0;

        if (numPlugins > kMaxSlots)
            numPlugins = kMaxSlots;

        // The inner vectors' buffers are what the slots point into.
        std::vector<Slot> slots;
        std::vector<std::vector<Function>> functions;
        std::vector<std::vector<const char*>> natives;
        slots.reserve(numPlugins);
        functions.reserve(numPlugins * 2);
        natives.reserve(numPlugins);

        for (uint32_t p = 0; p < numPlugins; ++p)
        {
            Slot slot{};
            uint32_t entrySize, n;
            uint64_t runtime;
            uint8_t v2;
            const char* file;
            if (!in.U32(&entrySize) || !in.U64(&runtime) || !in.U8(&v2) || !in.Str(&file))
                break;
            slot.runtime = runtime;
            slot.v2 = v2 != 0;
            strncpy(slot.file, file, sizeof(slot.file) - 1);

            bool ok = in.U32(&n);
            auto& methods = functions.emplace_back();
            for (uint32_t i = 0; ok && i < n; ++i)
            {
                Function fn{};
                ok = in.U32(&fn.start) && in.U32(&fn.end) && in.Str(&fn.name);
                methods.push_back(fn);
            }
            ok = ok && in.U32(&n);
            auto& publics = functions.emplace_back();
            for (uint32_t i = 0; ok && i < n; ++i)
            {
                Function fn{};
                ok = in.U32(&fn.start) && in.Str(&fn.name);
                fn.end = fn.start;
                publics.push_back(fn);
            }
            ok = ok && in.U32(&n);
            auto& names = natives.emplace_back();
            for (uint32_t i = 0; ok && i < n; ++i)
            {
                const char* name = nullptr;
                ok = in.Str(&name);
                names.push_back(name);
            }
            if (!ok)
                break;

            slot.methods = methods.data();
            slot.numMethods = uint32_t(methods.size());
            slot.publics = publics.data();
            slot.numPublics = uint32_t(publics.size());
            slot.natives = names.data();
            slot.numNatives = uint32_t(names.size());
            slots.push_back(slot);
        }

        Model m{};
        m.slots = slots.data();
        m.numSlots = slots.size();
        if (haveState)
        {
            m.calls = state.calls;
            m.depth = state.depth;
            // The dump has no VM invoke frames; the innermost recorded call
            // is the runtime the JIT frames belong to.
            const uint32_t numCalls = m.depth < kMaxCallDepth ? m.depth : kMaxCallDepth;
            m.topRuntime = numCalls ? state.calls[numCalls - 1].runtime : 0;
            if (state.exitFpAddress)
                ReadDump(regions, count, state.exitFpAddress, &m.exitFp, sizeof(m.exitFp));
        }
        return DescribeModel(out, maxlen, m, stack);
    }
}
