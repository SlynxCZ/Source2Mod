/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * Crash-time view of the SourcePawn VM; see crashinfo.h.
 *
 * Project: Source2Mod
 */
#include "crashinfo.h"
#include "scriptplugin.h"

#include <base-runtime.h>
#include <environment.h>
#include <smx-image.h>
#include <stack-frames.h>
#include <x64/frames-x64.h>

#include <atomic>
#include <cstring>

namespace sourcepawn::crashinfo
{
    CallEntry g_calls[kMaxCallDepth];
    volatile uint32_t g_callDepth = 0;

    namespace
    {
        struct Function
        {
            const char* name;
            uint32_t start;
            uint32_t end;
        };

        // Written while the server runs, only read by the crash callback. A
        // slot is published by storing runtime last and retired by clearing
        // it first.
        struct Slot
        {
            sp::BaseRuntime* volatile runtime;
            bool v2;
            char file[128];
            // v2: by method index (the JIT frame id, and funcid >> 1).
            // v1: the RTTI methods when the binary has them, by code offset.
            Function* methods;
            uint32_t numMethods;
            // By public index (v1 funcid >> 1).
            Function* publics;
            uint32_t numPublics;
            const char** natives;
            uint32_t numNatives;
        };

        constexpr size_t kMaxSlots = 512;
        Slot g_slots[kMaxSlots];

        // Scan counts, kept off the (alternate) signal stack.
        uint32_t g_contextHits[kMaxSlots];

        const Slot* FindSlot(const void* runtime)
        {
            if (!runtime)
                return nullptr;
            for (const Slot& slot : g_slots)
            {
                if (slot.runtime == runtime)
                    return &slot;
            }
            return nullptr;
        }

        const char* FunctionByFuncId(const Slot* slot, funcid_t funcid)
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
                if (!str)
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
    }

    void Track(ScriptPlugin* plugin)
    {
        auto* runtime = static_cast<sp::BaseRuntime*>(plugin->Runtime());
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
            slot.methods = slot.numMethods ? new Function[slot.numMethods] : nullptr;
            for (uint32_t i = 0; i < slot.numMethods; ++i)
            {
                const sp::smx_rtti_method* method = image->GetMethod(i);
                if (method)
                    slot.methods[i] = { image->names() + method->name, method->pcode_start, method->pcode_end };
                else
                    slot.methods[i] = { nullptr, 0, 0 };
            }

            slot.numPublics = runtime->GetPublicsNum();
            slot.publics = slot.numPublics ? new Function[slot.numPublics] : nullptr;
            for (uint32_t i = 0; i < slot.numPublics; ++i)
            {
                sp_public_t* pub = nullptr;
                if (runtime->GetPublicByIndex(i, &pub) == SP_ERROR_NONE && pub)
                    slot.publics[i] = { pub->name, pub->code_offs, pub->code_offs };
                else
                    slot.publics[i] = { nullptr, 0, 0 };
            }

            slot.numNatives = runtime->GetNativesNum();
            slot.natives = slot.numNatives ? new const char*[slot.numNatives] : nullptr;
            for (uint32_t i = 0; i < slot.numNatives; ++i)
            {
                const sp_native_t* native = runtime->GetNative(i);
                slot.natives[i] = native ? native->name : nullptr;
            }

            std::atomic_thread_fence(std::memory_order_release);
            slot.runtime = runtime;
            return;
        }
    }

    void Untrack(ScriptPlugin* plugin)
    {
        const void* runtime = plugin->Runtime();
        for (Slot& slot : g_slots)
        {
            if (slot.runtime != runtime)
                continue;

            slot.runtime = nullptr;
            std::atomic_thread_fence(std::memory_order_release);
            delete[] slot.methods;
            delete[] slot.publics;
            delete[] slot.natives;
            slot.methods = nullptr;
            slot.publics = nullptr;
            slot.natives = nullptr;
            slot.numMethods = slot.numPublics = slot.numNatives = 0;
            return;
        }
    }

    size_t Describe(char* out, size_t maxlen, const StackCopy* stack)
    {
        Appender a(out, maxlen);
        a("-------- SOURCEPAWN BEGIN --------\n");

        // The native being called and the script frames under it, from the
        // JIT's own frames. exit_fp stays behind when a native returns, so it
        // counts only when it lies on the live part of the crashed stack.
        const char* nativeName = nullptr;
        bool inNative = false;
        bool inHelper = false;
        ScriptFrame frames[kMaxFrames];
        size_t numFrames = 0;

        sp::Environment* env = sp::Environment::get();
        sp::InvokeFrame* top = env ? env->top() : nullptr;
        const uint64_t exitFp = env ? reinterpret_cast<uint64_t>(env->exit_fp()) : 0;
        if (top && exitFp && stack && exitFp >= stack->sp && exitFp < stack->base + stack->size)
        {
            const Slot* slot = FindSlot(top->cx());
            uint64_t fp = exitFp;
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

        const uint32_t depth = g_callDepth;
        const uint32_t numCalls = depth < kMaxCallDepth ? depth : kMaxCallDepth;

        a("Blaming: ");
        if (numFrames)
        {
            a(SlotFile(frames[0].slot))("::")(frames[0].name);
            if (inNative)
                a(" (in native ")(nativeName)(")");
        }
        else if (numCalls)
        {
            const Slot* slot = FindSlot(g_calls[numCalls - 1].runtime);
            a(SlotFile(slot))("::")(FunctionByFuncId(slot, g_calls[numCalls - 1].funcid));
        }
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

        if (depth)
        {
            a("Script calls in progress (innermost first):\n");
            if (depth > kMaxCallDepth)
                a("  (").Dec(depth - kMaxCallDepth)(" deeper calls not recorded)\n");
            for (uint32_t i = numCalls; i-- > 0;)
            {
                const Slot* slot = FindSlot(g_calls[i].runtime);
                a("  [").Dec(numCalls - 1 - i)("] ")(SlotFile(slot))("::");
                if (const char* name = FunctionByFuncId(slot, g_calls[i].funcid))
                    a(name);
                else
                    a("<function ").Dec(uint32_t(g_calls[i].funcid))(">");
                a("\n");
            }
        }

        // What Throttle does with Accelerator's serialized contexts: a plugin
        // context pointer on the crashed stack is a plugin the thread was in.
        if (stack && stack->sp >= stack->base && stack->sp < stack->base + stack->size)
        {
            memset(g_contextHits, 0, sizeof(g_contextHits));
            const uint64_t end = stack->base + stack->size;
            for (uint64_t addr = (stack->sp + 7) & ~uint64_t(7); addr + 8 <= end; addr += 8)
            {
                uint64_t value;
                memcpy(&value, stack->bytes + (addr - stack->base), sizeof(value));
                if (!value)
                    continue;
                for (size_t i = 0; i < kMaxSlots; ++i)
                {
                    if (reinterpret_cast<uint64_t>(g_slots[i].runtime) == value)
                    {
                        g_contextHits[i]++;
                        break;
                    }
                }
            }

            bool any = false;
            for (size_t i = 0; i < kMaxSlots; ++i)
            {
                if (!g_contextHits[i] || !g_slots[i].runtime)
                    continue;
                if (!any)
                    a("Plugin contexts on the crashed stack:\n");
                any = true;
                a("  ")(g_slots[i].file)(" (").Dec(g_contextHits[i])(")\n");
            }
            if (!any)
                a("Plugin contexts on the crashed stack: none\n");
        }
        else
            a("Plugin contexts on the crashed stack: <stack not available>\n");

        a("-------- SOURCEPAWN END --------\n");
        return a.Length();
    }
}
