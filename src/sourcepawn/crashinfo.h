/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * What the crash handler can say about SourcePawn. Script code runs as JIT in
 * anonymous memory, so a crash it causes shows up as source2mod (the native
 * it called) or as an unknown frame. Like SourceMod's Accelerator, which
 * serialized the plugin contexts for Throttle, the core keeps a table of the
 * loaded scripts the crash callback can read without allocating or locking,
 * and a record of the script calls in progress. The native a script is
 * inside needs no bookkeeping of ours: the JIT already stores it in the exit
 * frame it pushes before every native call.
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

    struct CallEntry
    {
        SourcePawn::IPluginRuntime* runtime;
        funcid_t funcid;
    };

    constexpr uint32_t kMaxCallDepth = 32;
    extern CallEntry g_calls[kMaxCallDepth];
    extern volatile uint32_t g_callDepth;

    /// Around every call from the host into a script: two stores in, one out.
    class CallScope
    {
    public:
        explicit CallScope(SourcePawn::IPluginFunction* fn)
        {
            const uint32_t depth = g_callDepth;
            if (depth < kMaxCallDepth)
                g_calls[depth] = { fn->GetParentRuntime(), fn->GetFunctionID() };
            g_callDepth = depth + 1;
        }
        ~CallScope() { g_callDepth = g_callDepth - 1; }

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
     * terminated). stack may be null when the dump could not be read. Reads
     * only what was prepared before the crash; no allocation, no locks.
     */
    size_t Describe(char* out, size_t maxlen, const StackCopy* stack);
}
