/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * What the crash handler can say about SourcePawn. Script code runs as JIT in
 * anonymous memory, so a crash it causes shows up as source2mod (the native
 * it called) or as an unknown frame. SourceMod's Accelerator (Asher Baker)
 * is the model: it serialized every plugin context -- the context pointer,
 * the file, the publics -- into a block it registered with breakpad, so the
 * dump itself says which plugin a context pointer on the crashed stack
 * belongs to, and Throttle attributed the crash from that. The core does the
 * same with SourcePawn 2.0's richer metadata (every method, the natives),
 * adds a record of the script calls in progress, and lets the dump carry the
 * VM's exit frame pointer: the JIT stores the native it calls in the exit
 * frame it pushes, so that needs no bookkeeping of ours.
 *
 * Two readers: the crash callback (the SOURCEPAWN section of the .txt, from
 * live memory, no allocation) and the next start (from the dump alone, when
 * the callback could not write the section).
 *
 * Project: Source2Mod
 */
#pragma once

#include <sp_vm_api.h>

#include <cstddef>
#include <cstdint>

namespace sourcepawn
{
    class ScriptPlugin;
}

namespace sourcepawn::crashinfo
{
    /// A script came / is going: its file, functions and natives.
    void Track(ScriptPlugin* plugin);
    void Untrack(ScriptPlugin* plugin);

    /// The VM is going: its memory leaves the dump.
    void Shutdown();

    // Fixed layout: the dump carries it.
    struct CallEntry
    {
        uint64_t runtime;
        uint32_t funcid;
        uint32_t reserved;
    };

    constexpr uint32_t kMaxCallDepth = 32;
    constexpr uint64_t kStateMagic = 0x3165746174537053ULL;   // "SpState1"

    /// The live part the dump carries (registered with breakpad as app memory).
    struct State
    {
        uint64_t magic;
        uint64_t exitFpAddress;     // where the VM keeps exit_fp; its 8 bytes are registered too
        volatile uint32_t depth;
        uint32_t reserved;
        CallEntry calls[kMaxCallDepth];
    };
    extern State g_state;

    /// Around every call from the host into a script: two stores in, one out.
    class CallScope
    {
    public:
        explicit CallScope(SourcePawn::IPluginFunction* fn)
        {
            const uint32_t depth = g_state.depth;
            if (depth < kMaxCallDepth)
                g_state.calls[depth] = { reinterpret_cast<uintptr_t>(fn->GetParentRuntime()), static_cast<uint32_t>(fn->GetFunctionID()), 0 };
            g_state.depth = depth + 1;
        }
        ~CallScope() { g_state.depth = g_state.depth - 1; }

        CallScope(const CallScope&) = delete;
        CallScope& operator=(const CallScope&) = delete;
    };

    /// The crashed thread's stack as the minidump has it.
    struct StackCopy
    {
        const uint8_t* bytes;
        uint64_t base;
        uint64_t size;
        uint64_t sp;
    };

    /**
     * Writes the "SOURCEPAWN" section of the crash .txt into out (always
     * terminated) from live memory. stack may be null when the dump could
     * not be read. Reads only what was prepared before the crash; no
     * allocation, no locks.
     */
    size_t Describe(char* out, size_t maxlen, const StackCopy* stack);

    /// One memory region of a dump.
    struct DumpRegion
    {
        const uint8_t* bytes;
        uint64_t base;
        uint64_t size;
    };

    /**
     * The same section from a dump alone: the plugin table and the state are
     * found among its memory regions by their magic. Returns 0 when the dump
     * has no plugin table (no script was loaded, or a core without it).
     */
    size_t DescribeFromDump(char* out, size_t maxlen, const DumpRegion* regions, size_t count, const StackCopy* stack);
}
