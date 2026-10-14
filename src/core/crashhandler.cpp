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
 * Crash handling ported from AcceleratorLocal (Linux) and AcceleratorCS2
 * (Windows):
 *   - Slynx (˙·٠● S l y n x ●٠·˙) 2026
 *   - Poggu (Source2ZE) 2024-2026
 *   - Phoenix (˙·٠●Феникс●٠·˙) 2023-2025
 *   - Asher Baker (asherkin) 2011
 *
 * Project: Source2Mod
 */
#if defined(_WIN32)
// Breakpad's Windows client drags windows.h in, and its min/max macros would
// take std::min and numeric_limits::max below with them.
#define NOMINMAX
#endif

#include "crashhandler.h"

#include "schema/minidump.h"

#include "http.h"
#include "shared.h"
#include "utils/log.h"
#include "utils/paths.h"

#include "interfaces/interfaces.h"
#include "networksystem/inetworksystem.h"
#include "tier0/icommandline.h"
#include "tier0/memalloc.h"
#include "tier0/netadr.h"

#if defined(__linux__)
#include "client/linux/handler/exception_handler.h"
#include "common/linux/linux_libc_support.h"
#include "third_party/lss/linux_syscall_support.h"
#include "common/linux/dump_symbols.h"

#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#elif defined(_WIN32)
#include "client/windows/handler/exception_handler.h"

#include <windows.h>
#include <dbghelp.h>
#endif

#include "common/path_helper.h"
#include "common/using_std_string.h"
#include "google_breakpad/processor/basic_source_line_resolver.h"
#include "google_breakpad/processor/call_stack.h"
#include "google_breakpad/processor/minidump_processor.h"
#include "google_breakpad/processor/process_state.h"
#include "google_breakpad/processor/stack_frame.h"
#include "processor/pathname_stripper.h"
#include "processor/simple_symbol_supplier.h"
#include "processor/stackwalk_common.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
// In case something earlier in the include chain pulled windows.h in before
// NOMINMAX above was seen.
#undef min
#undef max
#endif

// The engine's own crash-comment buffer: LoggingSystem_GetLogCapture() fills it
// with the console history, which goes into the crash .txt. Exported by tier0
// on both platforms.
DLL_GLOBAL_IMPORT void LoggingSystem_GetLogCapture(CMiniDumpComment* pMiniDumpComment, bool bReversed);

namespace crashhandler
{
    namespace
    {
        using json = nlohmann::json;

        // The core's own requests. Plugin ids start at 1, so nothing
        // RemoveAllForPlugin() is ever called with can match this.
        constexpr PluginId kCoreOwner = 0;

        // Everything the crash-time callback reads has to be there before the
        // crash: no allocation, no std::string, no engine calls in there.
        char s_szCrashMap[256];
        char s_szCrashGamePath[512];
        char s_szCrashCommandLine[1024];
        char s_szDumpStoragePath[512];
        char s_szCrashMetaPath[560];

        char s_szDiscordWebhook[512];
        char s_szPendingCrashPath[560];
        char s_szSessionStatePath[560];

        google_breakpad::ExceptionHandler* s_pExceptionHandler = nullptr;
        CMiniDumpComment s_MiniDumpComment(95000);

        // The crash is processed during Init(), long before the Steam API
        // activates, so the report waits here until OnSteamAPIActivated().
        // The payload is assembled only at flush time: by then the server
        // socket is open and the network system knows the real server
        // address, which goes into the message.
        std::string s_strPendingReport;
        std::string s_strPendingTxtPath;

        /* =========================
        Shared helpers
        ========================= */

        // Anything living in addons/ is a third-party module: Source2Mod, a
        // plugin, another metamod plugin. Those are what gets symbolized and
        // the first of them in the stack is the suspected culprit.
        bool IsAddonsModule(const std::string& modulePath)
        {
            return modulePath.find("/addons/") != std::string::npos
                || modulePath.find("\\addons\\") != std::string::npos;
        }

        bool IsReadableFile(const std::string& path)
        {
            std::error_code ec;
            return std::filesystem::is_regular_file(path, ec);
        }

        void WriteSessionState(const char* pszState)
        {
            FILE* file = fopen(s_szSessionStatePath, "w");
            if (file)
            {
                fputs(pszState, file);
                fclose(file);
            }
        }

        // The metadata that goes next to every dump, written with plain stdio.
        // Only the Windows callback uses this directly -- the Linux one runs
        // inside a signal handler and has to stay on raw syscalls.
        void WriteCrashMetadata(FILE* extra)
        {
            fprintf(extra, "-------- CONFIG BEGIN --------");
            fprintf(extra, "\nMap=%s", s_szCrashMap);
            fprintf(extra, "\nGamePath=%s", s_szCrashGamePath);
            fprintf(extra, "\nCommandLine=%s", s_szCrashCommandLine);
            fprintf(extra, "\n-------- CONFIG END --------\n");
            fprintf(extra, "\n");

            LoggingSystem_GetLogCapture(&s_MiniDumpComment, true);
            const char* pszConsoleHistory = s_MiniDumpComment.GetStartPointer();

            if (pszConsoleHistory[0])
            {
                fprintf(extra, "-------- CONSOLE HISTORY BEGIN --------\n");
                fputs(pszConsoleHistory, extra);
                fprintf(extra, "-------- CONSOLE HISTORY END --------\n");
                fprintf(extra, "\n");
            }
        }

#if defined(__linux__)

        /* =========================
        Linux: signal-time dump
        ========================= */

        void (*s_pfnSignalHandler)(int, siginfo_t*, void*) = nullptr;
        const int kExceptionSignals[] = { SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS };
        const int kNumHandledSignals = std::size(kExceptionSignals);

        bool dumpCallback(const google_breakpad::MinidumpDescriptor& descriptor, void* context, bool succeeded)
        {
            if (succeeded)
                sys_write(STDOUT_FILENO, "Wrote minidump to: ", 19);
            else
                sys_write(STDOUT_FILENO, "Failed to write minidump to: ", 29);

            sys_write(STDOUT_FILENO, descriptor.path(), my_strlen(descriptor.path()));
            sys_write(STDOUT_FILENO, "\n", 1);

            if (!succeeded)
                return succeeded;

            // Leave a marker so the next server start knows there is an unprocessed crash.
            int pending = sys_open(s_szPendingCrashPath, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
            if (pending != -1)
            {
                sys_write(pending, descriptor.path(), my_strlen(descriptor.path()));
                sys_close(pending);
            }

            my_strlcpy(s_szCrashMetaPath, descriptor.path(), sizeof(s_szCrashMetaPath));
            my_strlcat(s_szCrashMetaPath, ".txt", sizeof(s_szCrashMetaPath));

            int extra = sys_open(s_szCrashMetaPath, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
            if (extra == -1)
            {
                sys_write(STDOUT_FILENO, "Failed to open metadata file!\n", 30);
                return succeeded;
            }

            sys_write(extra, "-------- CONFIG BEGIN --------", 30);
            sys_write(extra, "\nMap=", 5);
            sys_write(extra, s_szCrashMap, my_strlen(s_szCrashMap));
            sys_write(extra, "\nGamePath=", 10);
            sys_write(extra, s_szCrashGamePath, my_strlen(s_szCrashGamePath));
            sys_write(extra, "\nCommandLine=", 13);
            sys_write(extra, s_szCrashCommandLine, my_strlen(s_szCrashCommandLine));
            sys_write(extra, "\n-------- CONFIG END --------\n", 30);
            sys_write(extra, "\n", 1);

            LoggingSystem_GetLogCapture(&s_MiniDumpComment, true);
            const char* pszConsoleHistory = s_MiniDumpComment.GetStartPointer();

            if (pszConsoleHistory[0])
            {
                sys_write(extra, "-------- CONSOLE HISTORY BEGIN --------\n", 40);
                sys_write(extra, pszConsoleHistory, my_strlen(pszConsoleHistory));
                sys_write(extra, "-------- CONSOLE HISTORY END --------\n", 38);
                sys_write(extra, "\n", 1);
            }

            std::shared_ptr<google_breakpad::SimpleSymbolSupplier> symbolSupplier;
            google_breakpad::BasicSourceLineResolver resolver;
            google_breakpad::MinidumpProcessor minidump_processor(symbolSupplier.get(), &resolver);

            // Increase the maximum number of threads and regions.
            google_breakpad::MinidumpThreadList::set_max_threads(std::numeric_limits<uint32_t>::max());
            google_breakpad::MinidumpMemoryList::set_max_regions(std::numeric_limits<uint32_t>::max());
            // Process the minidump.
            google_breakpad::Minidump miniDump(descriptor.path());
            if (!miniDump.Read())
            {
                sys_write(STDOUT_FILENO, "Failed to read minidump\n", 24);
            }
            else
            {
                google_breakpad::ProcessState processState;
                if (minidump_processor.Process(&miniDump, &processState) != google_breakpad::PROCESS_OK)
                {
                    sys_write(STDOUT_FILENO, "MinidumpProcessor::Process failed\n", 34);
                }
                else
                {
                    int requestingThread = processState.requesting_thread();
                    if (requestingThread == -1)
                        requestingThread = 0;

                    const google_breakpad::CallStack* stack = processState.threads()->at(requestingThread);
                    size_t frameCount = std::min<size_t>(stack->frames()->size(), 15);

                    auto signal_safe_hex_print = [](uint64_t num)
                    {
                        char buffer[18];
                        char* ptr = buffer + sizeof(buffer);

                        if (num == 0)
                            *(--ptr) = '0';
                        else
                        {
                            while (num > 0)
                            {
                                *(--ptr) = "0123456789abcdef"[num % 16];
                                num /= 16;
                            }
                        }

                        *(--ptr) = 'x';
                        *(--ptr) = '0';

                        size_t length = buffer + sizeof(buffer) - ptr;
                        sys_write(STDOUT_FILENO, ptr, length);
                    };

                    sys_write(STDOUT_FILENO, "\n", 1);
                    for (size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
                    {
                        const google_breakpad::StackFrame* frame = stack->frames()->at(frameIndex);

                        uint64_t moduleOffset = frame->ReturnAddress();
                        if (frame->module)
                        {
                            const std::string moduleFile = google_breakpad::PathnameStripper::File(frame->module->code_file());
                            moduleOffset -= frame->module->base_address();
                            sys_write(STDOUT_FILENO, moduleFile.c_str(), moduleFile.size());
                            sys_write(STDOUT_FILENO, " + ", 3);
                            signal_safe_hex_print(moduleOffset);
                            sys_write(STDOUT_FILENO, "\n", 1);
                        }
                        else
                        {
                            sys_write(STDOUT_FILENO, "unknown + ", 10);
                            signal_safe_hex_print(moduleOffset);
                            sys_write(STDOUT_FILENO, "\n", 1);
                        }
                    }

                    freopen(s_szCrashMetaPath, "a", stdout);
                    PrintProcessState(processState, true, false, &resolver);
                    fflush(stdout);
                }
            }

            sys_close(extra);

            return succeeded;
        }

        /* =========================
        Linux: symbolization
        ========================= */

        bool RunCommandCapture(const char* pszCommand, char* pszOut, size_t maxlen)
        {
            pszOut[0] = '\0';

            FILE* pipe = popen(pszCommand, "r");
            if (!pipe)
                return false;

            size_t total = fread(pszOut, 1, maxlen - 1, pipe);
            pszOut[total] = '\0';

            return pclose(pipe) == 0 && total > 0;
        }

        // Paths end up inside single quotes in a shell command, so refuse anything that could break out.
        bool IsShellSafe(const char* psz)
        {
            for (; *psz; ++psz)
            {
                if (*psz == '\'' || *psz == '\\' || *psz == '\n' || *psz == '\r')
                    return false;
            }

            return true;
        }

        // Returns "llvm-symbolizer", "addr2line" or nullptr depending on what is available on the machine.
        const char* FindSymbolizerTool()
        {
            static char szTool[32];

            char szOut[256];
            if (RunCommandCapture("command -v llvm-symbolizer 2>/dev/null", szOut, sizeof(szOut)))
            {
                strncpy(szTool, "llvm-symbolizer", sizeof(szTool) - 1);
                return szTool;
            }

            if (RunCommandCapture("command -v addr2line 2>/dev/null", szOut, sizeof(szOut)))
            {
                strncpy(szTool, "addr2line", sizeof(szTool) - 1);
                return szTool;
            }

            return nullptr;
        }

        bool SymbolizeWithTool(const char* pszTool, const char* pszModulePath, uint64_t offset, char* pszOut, size_t maxlen)
        {
            pszOut[0] = '\0';

            if (!pszTool || !IsShellSafe(pszModulePath) || !IsReadableFile(pszModulePath))
                return false;

            char szCommand[1024];
            if (strcmp(pszTool, "llvm-symbolizer") == 0)
                snprintf(szCommand, sizeof(szCommand), "llvm-symbolizer --obj='%s' 0x%llx 2>/dev/null", pszModulePath, static_cast<unsigned long long>(offset));
            else
                snprintf(szCommand, sizeof(szCommand), "addr2line -f -C -e '%s' 0x%llx 2>/dev/null", pszModulePath, static_cast<unsigned long long>(offset));

            char szOutput[1024];
            if (!RunCommandCapture(szCommand, szOutput, sizeof(szOutput)))
                return false;

            // First line is the function name, second line is file:line.
            char* pszFunction = szOutput;
            char* pszLocation = strchr(szOutput, '\n');
            if (pszLocation)
            {
                *pszLocation++ = '\0';
                char* pszEnd = strchr(pszLocation, '\n');
                if (pszEnd)
                    *pszEnd = '\0';
            }

            if (!pszFunction[0] || strcmp(pszFunction, "??") == 0)
                return false;

            if (pszLocation && pszLocation[0] && strncmp(pszLocation, "??", 2) != 0)
                snprintf(pszOut, maxlen, "%s @ %s", pszFunction, pszLocation);
            else
                snprintf(pszOut, maxlen, "%s", pszFunction);

            return true;
        }

        /// Symbolizes addons/ modules in-process with the bundled breakpad:
        /// symbols are extracted straight from the ELF/DWARF on disk, so
        /// nothing besides Source2Mod is needed inside the steamrt
        /// container. llvm-symbolizer / addr2line stay as a fallback for
        /// modules that pass could not handle.
        class AddonsSymbolizer
        {
        public:
            void Load(const google_breakpad::CallStack* stack, size_t frameCount)
            {
                // Breakpad's demangler predates C++20 mangling and floods stderr with thousands of
                // "failed to demangle" warnings on modern binaries, so stderr is muted while dumping.
                int savedStderr = dup(STDERR_FILENO);
                int devNull = open("/dev/null", O_WRONLY);
                if (devNull != -1)
                    dup2(devNull, STDERR_FILENO);

                for (size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
                {
                    google_breakpad::StackFrame* frame = stack->frames()->at(frameIndex);
                    if (!frame->module || m_Resolver.HasModule(frame->module))
                        continue;

                    const std::string modulePath = frame->module->code_file();
                    if (!IsAddonsModule(modulePath) || !IsReadableFile(modulePath))
                        continue;

                    std::ostringstream symbolStream;
                    google_breakpad::DumpOptions options(SYMBOLS_AND_FILES, true, false, false);
                    if (google_breakpad::WriteSymbolFile(modulePath, modulePath, "Linux", "", std::vector<string>(), options, symbolStream))
                        m_Resolver.LoadModuleUsingMapBuffer(frame->module, symbolStream.str());
                }

                if (devNull != -1)
                {
                    if (savedStderr != -1)
                        dup2(savedStderr, STDERR_FILENO);
                    close(devNull);
                }
                if (savedStderr != -1)
                    close(savedStderr);

                m_pszTool = FindSymbolizerTool();
            }

            bool Resolve(google_breakpad::StackFrame* frame, const std::string& modulePath, uint64_t moduleOffset, char* pszOut, size_t maxlen)
            {
                pszOut[0] = '\0';

                m_Resolver.FillSourceLineInfo(frame, nullptr);

                if (frame->function_name.empty())
                    return SymbolizeWithTool(m_pszTool, modulePath.c_str(), moduleOffset, pszOut, maxlen);

                if (!frame->source_file_name.empty())
                    snprintf(pszOut, maxlen, "%s @ %s:%d", frame->function_name.c_str(),
                             google_breakpad::PathnameStripper::File(frame->source_file_name).c_str(), frame->source_line);
                else
                    snprintf(pszOut, maxlen, "%s", frame->function_name.c_str());

                return true;
            }

        private:
            google_breakpad::BasicSourceLineResolver m_Resolver;
            const char* m_pszTool = nullptr;
        };

#elif defined(_WIN32)

        /* =========================
        Windows: exception-time dump
        ========================= */

        PVOID s_pVectoredHandler = nullptr;

        std::wstring Widen(const char* psz)
        {
            int needed = MultiByteToWideChar(CP_UTF8, 0, psz, -1, nullptr, 0);
            if (needed <= 0)
                return std::wstring();

            std::wstring wide(static_cast<size_t>(needed - 1), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, psz, -1, wide.data(), needed);
            return wide;
        }

        void Narrow(const wchar_t* pwsz, char* pszOut, int maxlen)
        {
            int written = WideCharToMultiByte(CP_UTF8, 0, pwsz, -1, pszOut, maxlen, nullptr, nullptr);
            if (written <= 0)
                pszOut[0] = '\0';
            pszOut[maxlen - 1] = '\0';
        }

        // Runs on breakpad's handler thread, so ordinary stdio is fine here.
        bool dumpCallback(const wchar_t* dump_path, const wchar_t* minidump_id, void* context,
                          EXCEPTION_POINTERS* exinfo, MDRawAssertionInfo* assertion, bool succeeded)
        {
            if (!succeeded)
            {
                printf("Failed to write minidump to: %ls\\%ls.dmp\n", dump_path, minidump_id);
                return succeeded;
            }

            char szDumpPath[560];
            snprintf(szDumpPath, sizeof(szDumpPath), "%ls\\%ls.dmp", dump_path, minidump_id);
            printf("Wrote minidump to: %s\n", szDumpPath);

            // Leave a marker so the next server start knows there is an unprocessed crash.
            FILE* pending = fopen(s_szPendingCrashPath, "w");
            if (pending)
            {
                fputs(szDumpPath, pending);
                fclose(pending);
            }

            snprintf(s_szCrashMetaPath, sizeof(s_szCrashMetaPath), "%s.txt", szDumpPath);

            FILE* extra = fopen(s_szCrashMetaPath, "wb");
            if (!extra)
            {
                printf("Failed to open metadata file!\n");
                return succeeded;
            }

            WriteCrashMetadata(extra);

            std::shared_ptr<google_breakpad::SimpleSymbolSupplier> symbolSupplier;
            google_breakpad::BasicSourceLineResolver resolver;
            google_breakpad::MinidumpProcessor minidump_processor(symbolSupplier.get(), &resolver);

            // Increase the maximum number of threads and regions.
            google_breakpad::MinidumpThreadList::set_max_threads(std::numeric_limits<uint32_t>::max());
            google_breakpad::MinidumpMemoryList::set_max_regions(std::numeric_limits<uint32_t>::max());
            // Process the minidump.
            google_breakpad::Minidump miniDump(szDumpPath);
            if (!miniDump.Read())
            {
                printf("Failed to read minidump\n");
            }
            else
            {
                google_breakpad::ProcessState processState;
                if (minidump_processor.Process(&miniDump, &processState) != google_breakpad::PROCESS_OK)
                {
                    printf("MinidumpProcessor::Process failed\n");
                }
                else
                {
                    int requestingThread = processState.requesting_thread();
                    if (requestingThread == -1)
                        requestingThread = 0;

                    const google_breakpad::CallStack* stack = processState.threads()->at(requestingThread);
                    size_t frameCount = std::min<size_t>(stack->frames()->size(), 15);

                    printf("\n");
                    for (size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
                    {
                        const google_breakpad::StackFrame* frame = stack->frames()->at(frameIndex);

                        uint64_t moduleOffset = frame->ReturnAddress();
                        if (frame->module)
                        {
                            const std::string moduleFile = google_breakpad::PathnameStripper::File(frame->module->code_file());
                            moduleOffset -= frame->module->base_address();
                            printf("%s + 0x%llx\n", moduleFile.c_str(), static_cast<unsigned long long>(moduleOffset));
                        }
                        else
                            printf("unknown + 0x%llx\n", static_cast<unsigned long long>(moduleOffset));
                    }

                    fflush(stdout);
                    freopen(s_szCrashMetaPath, "a", stdout);
                    PrintProcessState(processState, true, false, &resolver);
                    fflush(stdout);
                }
            }

            fclose(extra);

            return succeeded;
        }

        // Valve installs its own unhandled-exception filter that writes a dump
        // of its own and exits, so breakpad's would never get a turn. A
        // vectored handler runs before any of that: it writes the dump for the
        // exceptions worth one and then tells the rest of the chain to stay
        // out of it. Straight from AcceleratorCS2.
        LONG CALLBACK BreakpadVectoredHandler(PEXCEPTION_POINTERS ExceptionInfo)
        {
            switch (ExceptionInfo->ExceptionRecord->ExceptionCode)
            {
                case EXCEPTION_ACCESS_VIOLATION:
                case EXCEPTION_INVALID_HANDLE:
                case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
                case EXCEPTION_DATATYPE_MISALIGNMENT:
                case EXCEPTION_ILLEGAL_INSTRUCTION:
                case EXCEPTION_INT_DIVIDE_BY_ZERO:
                case EXCEPTION_STACK_OVERFLOW:
                case 0xC0000409: // STATUS_STACK_BUFFER_OVERRUN
                case 0xC0000374: // STATUS_HEAP_CORRUPTION
                    break;
                case 0: // Valve use this for Sys_Error.
                    if ((ExceptionInfo->ExceptionRecord->ExceptionFlags & EXCEPTION_NONCONTINUABLE) == 0)
                        return EXCEPTION_CONTINUE_SEARCH;
                    break;
                default:
                    return EXCEPTION_CONTINUE_SEARCH;
            }

            if (s_pExceptionHandler && s_pExceptionHandler->WriteMinidumpForException(ExceptionInfo))
            {
                // Stop the handler thread from deadlocking us.
                delete s_pExceptionHandler;
                s_pExceptionHandler = nullptr;

                // Stop Valve's handler being called.
                ExceptionInfo->ExceptionRecord->ExceptionCode = EXCEPTION_BREAKPOINT;

                return EXCEPTION_EXECUTE_HANDLER;
            }

            return EXCEPTION_CONTINUE_SEARCH;
        }

        /* =========================
        Windows: symbolization
        ========================= */

        /// Symbolizes addons/ modules through DbgHelp, which reads the PDB
        /// that sits next to the DLL (or wherever the DLL's debug directory
        /// points). Nothing to install: dbghelp.dll ships with Windows. A
        /// module without a PDB stays module + offset.
        class AddonsSymbolizer
        {
        public:
            ~AddonsSymbolizer()
            {
                if (m_bInitialized)
                    SymCleanup(GetCurrentProcess());
            }

            void Load(const google_breakpad::CallStack* stack, size_t frameCount)
            {
                SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS);
                m_bInitialized = SymInitialize(GetCurrentProcess(), nullptr, FALSE) != FALSE;
            }

            bool Resolve(google_breakpad::StackFrame* frame, const std::string& modulePath, uint64_t moduleOffset, char* pszOut, size_t maxlen)
            {
                pszOut[0] = '\0';

                if (!m_bInitialized || !IsReadableFile(modulePath))
                    return false;

                DWORD64 base = LoadModule(modulePath);
                if (!base)
                    return false;

                HANDLE hProcess = GetCurrentProcess();
                DWORD64 address = base + moduleOffset;

                alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
                SYMBOL_INFO* pSymbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
                pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
                pSymbol->MaxNameLen = MAX_SYM_NAME;

                DWORD64 symbolDisplacement = 0;
                if (!SymFromAddr(hProcess, address, &symbolDisplacement, pSymbol))
                    return false;

                IMAGEHLP_LINE64 line = {};
                line.SizeOfStruct = sizeof(line);
                DWORD lineDisplacement = 0;

                if (SymGetLineFromAddr64(hProcess, address, &lineDisplacement, &line) && line.FileName)
                    snprintf(pszOut, maxlen, "%s @ %s:%lu", pSymbol->Name,
                             google_breakpad::PathnameStripper::File(line.FileName).c_str(), static_cast<unsigned long>(line.LineNumber));
                else
                    snprintf(pszOut, maxlen, "%s", pSymbol->Name);

                return true;
            }

        private:
            // Every x64 DLL prefers the same image base, so loading them at
            // their own would collide. Each gets its own 4 GB slot instead;
            // the offsets from the dump are relative to the base either way.
            DWORD64 LoadModule(const std::string& modulePath)
            {
                if (auto it = m_Loaded.find(modulePath); it != m_Loaded.end())
                    return it->second;

                DWORD64 slot = 0x7FF000000000ULL + static_cast<DWORD64>(m_Loaded.size()) * 0x100000000ULL;
                DWORD64 base = SymLoadModuleEx(GetCurrentProcess(), nullptr, modulePath.c_str(), nullptr, slot, 0, nullptr, 0);

                m_Loaded.emplace(modulePath, base);
                return base;
            }

            bool m_bInitialized = false;
            std::map<std::string, DWORD64> m_Loaded;
        };

#endif

        /* =========================
        Discord
        ========================= */

        void GetServerAddress(char* pszOut, size_t maxlen)
        {
            char szIp[64] = {};
            char szPort[16] = {};

            if (g_pNetworkSystem)
            {
                const netadr_t& publicAdr = g_pNetworkSystem->GetPublicAdr();
                const netadr_t& adr = (publicAdr.ip[0] | publicAdr.ip[1] | publicAdr.ip[2] | publicAdr.ip[3])
                                          ? publicAdr
                                          : g_pNetworkSystem->GetLocalAdr();

                if (adr.ip[0] | adr.ip[1] | adr.ip[2] | adr.ip[3])
                    snprintf(szIp, sizeof(szIp), "%hhu.%hhu.%hhu.%hhu", adr.ip[0], adr.ip[1], adr.ip[2], adr.ip[3]);

                uint16 port = g_pNetworkSystem->GetUDPPort(1);
                if (port)
                    snprintf(szPort, sizeof(szPort), "%hu", port);
            }

            // Fall back to the launch parameters for whatever the network system couldn't provide.
            const char* pszCmdLine = CommandLine()->GetCmdLine();

            auto findArg = [pszCmdLine](const char* pszName, char* pszValue, size_t valuelen)
            {
                const char* psz = strstr(pszCmdLine, pszName);
                if (!psz)
                    return;

                psz += strlen(pszName);
                while (*psz == ' ')
                    ++psz;

                size_t i = 0;
                while (*psz && *psz != ' ' && *psz != '"' && i < valuelen - 1)
                    pszValue[i++] = *psz++;
                pszValue[i] = '\0';
            };

            if (!szIp[0])
                findArg("-ip ", szIp, sizeof(szIp));
            if (!szPort[0])
            {
                findArg("-port ", szPort, sizeof(szPort));
                if (!szPort[0])
                    findArg("+port ", szPort, sizeof(szPort));
            }

            snprintf(pszOut, maxlen, "%s:%s", szIp[0] ? szIp : "0.0.0.0", szPort[0] ? szPort : "27015");
        }

        void FlushPendingDiscordReport()
        {
            if (s_strPendingReport.empty() || !http::httpManager.IsAvailable())
                return;

            char szAddress[80];
            GetServerAddress(szAddress, sizeof(szAddress));

            json payload;
            payload["content"] = "Server `" + std::string(szAddress) + "` crashed\n```\n" + s_strPendingReport + "\n```";

            std::string txtData;
            if (!s_strPendingTxtPath.empty())
            {
                FILE* file = fopen(s_strPendingTxtPath.c_str(), "rb");
                if (file)
                {
                    char buffer[8192];
                    size_t bytes;
                    while ((bytes = fread(buffer, 1, sizeof(buffer), file)) > 0)
                        txtData.append(buffer, bytes);
                    fclose(file);
                }
            }

            std::string body;
            std::string contentType;

            if (!txtData.empty())
            {
                // Discord needs multipart/form-data for attachments and Steam HTTP only takes
                // a raw body, so the multipart body is assembled by hand.
                const char* pszBoundary = "Source2ModCrashBoundary7MA4YWxkTrZu0gW";

                body += "--"; body += pszBoundary; body += "\r\n";
                body += "Content-Disposition: form-data; name=\"payload_json\"\r\n";
                body += "Content-Type: application/json\r\n\r\n";
                body += payload.dump();
                body += "\r\n--"; body += pszBoundary; body += "\r\n";
                body += "Content-Disposition: form-data; name=\"files[0]\"; filename=\"crash.txt\"\r\n";
                body += "Content-Type: text/plain\r\n\r\n";
                body += txtData;
                body += "\r\n--"; body += pszBoundary; body += "--\r\n";

                contentType = std::string("multipart/form-data; boundary=") + pszBoundary;
            }
            else
            {
                body = payload.dump();
                contentType = "application/json";
            }

            http::httpManager.PostRaw(kCoreOwner, s_szDiscordWebhook, contentType.c_str(),
                                      body.data(), body.size(),
                                      [](const Source2ModHTTPResponse& response)
                                      {
                                          if (response.m_bSuccess && response.m_nStatusCode >= 200 && response.m_nStatusCode <= 299)
                                              FP_INFO("Crash report sent to Discord.");
                                          else
                                              FP_WARN("Discord webhook failed (HTTP {}).", response.m_nStatusCode);
                                      });

            s_strPendingReport.clear();
            s_strPendingTxtPath.clear();
        }

        void SendDiscordReport(const char* pszReport, const char* pszTxtPath)
        {
            if (!s_szDiscordWebhook[0])
                return;

            // Discord message limit is 2000 characters, keep some headroom for the code
            // fences and the address line added at flush time.
            std::string content(pszReport);
            if (content.size() > 1700)
                content.resize(1700);

            s_strPendingReport = std::move(content);
            s_strPendingTxtPath = pszTxtPath ? pszTxtPath : "";

            if (http::httpManager.IsAvailable())
                FlushPendingDiscordReport();
            else
                FP_INFO("Crash report queued, will be sent to Discord once the Steam API activates.");
        }

        /* =========================
        Startup: process the last crash
        ========================= */

        void ProcessPendingCrash(bool bPrevSessionStarted)
        {
            char szDumpPath[512] = {};

            FILE* file = fopen(s_szPendingCrashPath, "r");
            if (!file)
                return;

            size_t total = fread(szDumpPath, 1, sizeof(szDumpPath) - 1, file);
            fclose(file);
            szDumpPath[total] = '\0';

            // Remove the marker first so a crash below can never loop forever.
            remove(s_szPendingCrashPath);

            if (!szDumpPath[0])
                return;

            if (!bPrevSessionStarted)
            {
                FP_WARN("Crash dump {} left unprocessed: previous session crashed before the server finished starting (crash loop protection).", szDumpPath);
                return;
            }

            FP_WARN("Server crashed last session, processing {}", szDumpPath);

            google_breakpad::BasicSourceLineResolver resolver;
            google_breakpad::MinidumpProcessor processor(nullptr, &resolver);
            google_breakpad::Minidump miniDump(szDumpPath);

            google_breakpad::ProcessState processState;
            if (!miniDump.Read() || processor.Process(&miniDump, &processState) != google_breakpad::PROCESS_OK)
            {
                FP_ERROR("Failed to process the crash dump.");
                return;
            }

            int requestingThread = processState.requesting_thread();
            if (requestingThread == -1)
                requestingThread = 0;

            const google_breakpad::CallStack* stack = processState.threads()->at(requestingThread);
            size_t frameCount = std::min<size_t>(stack->frames()->size(), 10);

            AddonsSymbolizer symbolizer;
            symbolizer.Load(stack, frameCount);

            char szReport[3072];
            snprintf(szReport, sizeof(szReport), "Server crashed: %s @ 0x%llx\n\n",
                     processState.crash_reason().c_str(), static_cast<unsigned long long>(processState.crash_address()));

            char szCulprit[768] = {};

            for (size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
            {
                google_breakpad::StackFrame* frame = stack->frames()->at(frameIndex);

                char szLine[1024];
                uint64_t moduleOffset = frame->ReturnAddress();

                if (frame->module)
                {
                    const std::string modulePath = frame->module->code_file();
                    const std::string moduleFile = google_breakpad::PathnameStripper::File(modulePath);
                    moduleOffset -= frame->module->base_address();

                    // Anything living in addons/ is a third-party plugin, so it can be symbolized locally
                    // and the first such frame is the most likely culprit.
                    char szSymbol[512] = {};
                    if (IsAddonsModule(modulePath))
                    {
                        symbolizer.Resolve(frame, modulePath, moduleOffset, szSymbol, sizeof(szSymbol));

                        if (!szCulprit[0])
                        {
                            if (szSymbol[0])
                                snprintf(szCulprit, sizeof(szCulprit), "%s -> %s", moduleFile.c_str(), szSymbol);
                            else
                                snprintf(szCulprit, sizeof(szCulprit), "%s + 0x%llx", moduleFile.c_str(), static_cast<unsigned long long>(moduleOffset));
                        }
                    }

                    if (szSymbol[0])
                        snprintf(szLine, sizeof(szLine), "#%zu %s + 0x%llx (%s)\n", frameIndex, moduleFile.c_str(), static_cast<unsigned long long>(moduleOffset), szSymbol);
                    else
                        snprintf(szLine, sizeof(szLine), "#%zu %s + 0x%llx\n", frameIndex, moduleFile.c_str(), static_cast<unsigned long long>(moduleOffset));
                }
                else
                    snprintf(szLine, sizeof(szLine), "#%zu unknown + 0x%llx\n", frameIndex, static_cast<unsigned long long>(moduleOffset));

                strncat(szReport, szLine, sizeof(szReport) - strlen(szReport) - 1);
            }

            if (szCulprit[0])
            {
                strncat(szReport, "\nSuspected culprit: ", sizeof(szReport) - strlen(szReport) - 1);
                strncat(szReport, szCulprit, sizeof(szReport) - strlen(szReport) - 1);
                strncat(szReport, "\n", sizeof(szReport) - strlen(szReport) - 1);
            }
            else
                strncat(szReport, "\n(no third-party module found in the crash stack)\n", sizeof(szReport) - strlen(szReport) - 1);

            FP_WARN("---- Last crash ----\n{}--------------------", szReport);

            char szTxtPath[560];
            snprintf(szTxtPath, sizeof(szTxtPath), "%s.txt", szDumpPath);
            SendDiscordReport(szReport, szTxtPath);
        }

        /* =========================
        Platform: install / remove the handler
        ========================= */

        void InstallExceptionHandler()
        {
#if defined(__linux__)
            google_breakpad::MinidumpDescriptor descriptor(s_szDumpStoragePath);
            s_pExceptionHandler = new google_breakpad::ExceptionHandler(descriptor, NULL, dumpCallback, NULL, true, -1);

            struct sigaction oact;
            sigaction(SIGSEGV, NULL, &oact);
            s_pfnSignalHandler = oact.sa_sigaction;
#elif defined(_WIN32)
            s_pExceptionHandler = new google_breakpad::ExceptionHandler(
                Widen(s_szDumpStoragePath), NULL, dumpCallback, NULL,
                google_breakpad::ExceptionHandler::HANDLER_ALL,
                static_cast<MINIDUMP_TYPE>(MiniDumpWithUnloadedModules | MiniDumpWithFullMemoryInfo),
                static_cast<const wchar_t*>(NULL), NULL);

            s_pVectoredHandler = AddVectoredExceptionHandler(0, BreakpadVectoredHandler);
#endif
        }

        void RemoveExceptionHandler()
        {
#if defined(__linux__)
            s_pfnSignalHandler = nullptr;
#elif defined(_WIN32)
            if (s_pVectoredHandler)
            {
                RemoveVectoredExceptionHandler(s_pVectoredHandler);
                s_pVectoredHandler = nullptr;
            }
#endif

            delete s_pExceptionHandler;
            s_pExceptionHandler = nullptr;
        }
    }

    /* =========================
    Lifecycle
    ========================= */

    void Init()
    {
        if (!shared::g_pCoreConfig->CrashHandlerEnabled)
        {
            FP_INFO("Crash handler disabled by CrashHandlerEnabled.");
            return;
        }

        const std::string gameDirectory = paths::GameDirectory();
        const std::string dumpsDirectory = paths::GetDumpsDirectory();

        strncpy(s_szCrashGamePath, gameDirectory.c_str(), sizeof(s_szCrashGamePath) - 1);
        strncpy(s_szDumpStoragePath, dumpsDirectory.c_str(), sizeof(s_szDumpStoragePath) - 1);

        {
            std::error_code ec;
            std::filesystem::create_directories(dumpsDirectory, ec);

            if (!std::filesystem::is_directory(dumpsDirectory, ec))
            {
                FP_ERROR("Crash handler disabled: '{}' does not exist and could not be created.", s_szDumpStoragePath);
                return;
            }

#if defined(__linux__)
            chmod(s_szDumpStoragePath, 0777);
#endif
        }

        snprintf(s_szPendingCrashPath, sizeof(s_szPendingCrashPath), "%s/pending.state", s_szDumpStoragePath);
        snprintf(s_szSessionStatePath, sizeof(s_szSessionStatePath), "%s/session.state", s_szDumpStoragePath);

        strncpy(s_szDiscordWebhook, shared::g_pCoreConfig->CrashDiscordWebhook.c_str(), sizeof(s_szDiscordWebhook) - 1);

        if (!s_szDiscordWebhook[0])
            FP_INFO("CrashDiscordWebhook is empty, Discord crash reporting disabled.");

        // Was the previous session healthy (reached StartupServer) before it died?
        bool bPrevSessionStarted = false;
        {
            FILE* file = fopen(s_szSessionStatePath, "r");
            if (file)
            {
                char szState[16] = {};
                fread(szState, 1, sizeof(szState) - 1, file);
                fclose(file);
                bPrevSessionStarted = strncmp(szState, "started", 7) == 0;
            }
        }
        WriteSessionState("loading");

        ProcessPendingCrash(bPrevSessionStarted);

        InstallExceptionHandler();

        strncpy(s_szCrashCommandLine, CommandLine()->GetCmdLine(), sizeof(s_szCrashCommandLine) - 1);

        FP_INFO("Crash handler installed, dumps go to '{}'.", s_szDumpStoragePath);
    }

    void Shutdown()
    {
        RemoveExceptionHandler();

        s_strPendingReport.clear();
        s_strPendingTxtPath.clear();
    }

    void OnGameFrame()
    {
#if defined(__linux__)
        if (!s_pfnSignalHandler)
            return;

        bool bReplaced = false;
        struct sigaction oact;

        for (int i = 0; i < kNumHandledSignals; ++i)
        {
            sigaction(kExceptionSignals[i], NULL, &oact);

            if (oact.sa_sigaction != s_pfnSignalHandler)
            {
                bReplaced = true;
                break;
            }
        }

        if (!bReplaced)
            return;

        struct sigaction act;
        memset(&act, 0, sizeof(act));
        sigemptyset(&act.sa_mask);

        for (int i = 0; i < kNumHandledSignals; ++i)
            sigaddset(&act.sa_mask, kExceptionSignals[i]);

        act.sa_sigaction = s_pfnSignalHandler;
        act.sa_flags = SA_ONSTACK | SA_SIGINFO;

        for (int i = 0; i < kNumHandledSignals; ++i)
            sigaction(kExceptionSignals[i], &act, NULL);
#endif
        // Windows: the vectored handler cannot be displaced the way a signal
        // handler can, nothing to re-arm.
    }

    void OnStartupServer(const char* pszMapName)
    {
        if (!s_pExceptionHandler)
            return;

        strncpy(s_szCrashMap, pszMapName ? pszMapName : "", sizeof(s_szCrashMap) - 1);

        WriteSessionState("started");
    }

    void OnSteamAPIActivated()
    {
        FlushPendingDiscordReport();
    }
}
