cmake_minimum_required(VERSION 3.18)
project(source2mod C CXX ASM)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

# Source files are UTF-8 (the author strings); MSVC would read them as the ANSI code page.
if (MSVC)
    add_compile_options(/utf-8)
endif ()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(SPDLOG_HEADER_ONLY ON CACHE BOOL "" FORCE)
set(FMT_WERROR OFF CACHE BOOL "" FORCE)

function(resolve_dep_internal OUT_VAR ENV_NAME)
    if (DEFINED ENV{${ENV_NAME}} AND NOT "$ENV{${ENV_NAME}}" STREQUAL "")
        file(TO_CMAKE_PATH "$ENV{${ENV_NAME}}" _resolved)
        set(${OUT_VAR} "${_resolved}" PARENT_SCOPE)
        message(STATUS "${ENV_NAME} -> ${_resolved}")
    else ()
        message(FATAL_ERROR "${ENV_NAME} is not set")
    endif ()
endfunction()

resolve_dep_internal(SOURCE2MOD_SDK_DIR SOURCE2MOD_SDK)
resolve_dep_internal(METAMOD_DIR MMSOURCE_DEV)

# core build
set(SOURCE2MOD_CORE ON CACHE BOOL "" FORCE)
add_subdirectory(${SOURCE2MOD_SDK_DIR} ${CMAKE_BINARY_DIR}/source2mod-sdk)

# Hooks are KHook, metamod's own detour library: header-only here, the engine
# arrives through PLUGIN_SAVEVARS() and is handed on to Source2Mod's plugins.

add_subdirectory(vendor/dynlibutils)
add_subdirectory(vendor/spdlog)

file(GLOB_RECURSE SOURCE_FILES
        src/*.cpp
        src/*.h
        src/*.hpp
)

# dynhooks/ is work in progress and still written against SourceHook's own
# assembler and page allocator, which left with SourceHook. Out of the build
# until it is ported to KHook's (khook/asm, khook/memory).
list(FILTER SOURCE_FILES EXCLUDE REGEX "/src/core/dynhooks/")

# Crash handler (Linux only). The breakpad client and processor come prebuilt
# from vendor/breakpad-build; the in-process symbol dumper (ELF/DWARF to
# breakpad symbols, what lets a crash be symbolized without llvm-symbolizer
# or addr2line on the box) is not part of those libraries, so its sources
# are compiled straight into the core.
set(BREAKPAD_SOURCE_FILES "")
if (UNIX AND NOT APPLE)
    set(BREAKPAD_SRC ${CMAKE_SOURCE_DIR}/vendor/breakpad/src)
    set(BREAKPAD_SOURCE_FILES
            ${BREAKPAD_SRC}/common/dwarf_cfi_to_module.cc
            ${BREAKPAD_SRC}/common/dwarf_cu_to_module.cc
            ${BREAKPAD_SRC}/common/dwarf_line_to_module.cc
            ${BREAKPAD_SRC}/common/dwarf_range_list_handler.cc
            ${BREAKPAD_SRC}/common/language.cc
            ${BREAKPAD_SRC}/common/module.cc
            ${BREAKPAD_SRC}/common/path_helper.cc
            ${BREAKPAD_SRC}/common/stabs_reader.cc
            ${BREAKPAD_SRC}/common/stabs_to_module.cc
            ${BREAKPAD_SRC}/common/dwarf/bytereader.cc
            ${BREAKPAD_SRC}/common/dwarf/dwarf2diehandler.cc
            ${BREAKPAD_SRC}/common/dwarf/dwarf2reader.cc
            ${BREAKPAD_SRC}/common/dwarf/elf_reader.cc
            ${BREAKPAD_SRC}/common/linux/crc32.cc
            ${BREAKPAD_SRC}/common/linux/dump_symbols.cc
            ${BREAKPAD_SRC}/common/linux/elf_symbols_to_module.cc
    )
    # Hidden for the same reason the prebuilt libraries are --exclude-libs'd
    # below: the SDK's -fvisibility=default would otherwise export all of it.
    set_source_files_properties(${BREAKPAD_SOURCE_FILES} PROPERTIES
            LANGUAGE CXX
            COMPILE_OPTIONS "-fvisibility=hidden"
    )
elseif (WIN32)
    # No prebuilt libraries on Windows: the client, the processor and
    # libdisasm are built from the submodule with MSVC, the same set
    # AcceleratorCS2 builds. Symbolization goes through DbgHelp there, so
    # nothing of breakpad's PDB tooling (DIA) is needed.
    set(BREAKPAD_SRC ${CMAKE_SOURCE_DIR}/vendor/breakpad/src)
    add_library(breakpad-win STATIC
            ${BREAKPAD_SRC}/processor/basic_code_modules.cc
            ${BREAKPAD_SRC}/processor/basic_source_line_resolver.cc
            ${BREAKPAD_SRC}/processor/call_stack.cc
            ${BREAKPAD_SRC}/processor/cfi_frame_info.cc
            ${BREAKPAD_SRC}/processor/convert_old_arm64_context.cc
            ${BREAKPAD_SRC}/processor/disassembler_x86.cc
            ${BREAKPAD_SRC}/processor/dump_context.cc
            ${BREAKPAD_SRC}/processor/dump_object.cc
            ${BREAKPAD_SRC}/processor/exploitability.cc
            ${BREAKPAD_SRC}/processor/exploitability_linux.cc
            ${BREAKPAD_SRC}/processor/exploitability_win.cc
            ${BREAKPAD_SRC}/processor/fast_source_line_resolver.cc
            ${BREAKPAD_SRC}/processor/logging.cc
            ${BREAKPAD_SRC}/processor/microdump.cc
            ${BREAKPAD_SRC}/processor/microdump_processor.cc
            ${BREAKPAD_SRC}/processor/minidump.cc
            ${BREAKPAD_SRC}/processor/minidump_processor.cc
            ${BREAKPAD_SRC}/processor/module_comparer.cc
            ${BREAKPAD_SRC}/processor/module_serializer.cc
            ${BREAKPAD_SRC}/processor/pathname_stripper.cc
            ${BREAKPAD_SRC}/processor/process_state.cc
            ${BREAKPAD_SRC}/processor/proc_maps_linux.cc
            ${BREAKPAD_SRC}/processor/simple_symbol_supplier.cc
            ${BREAKPAD_SRC}/processor/source_line_resolver_base.cc
            ${BREAKPAD_SRC}/processor/stack_frame_cpu.cc
            ${BREAKPAD_SRC}/processor/stack_frame_symbolizer.cc
            ${BREAKPAD_SRC}/processor/stackwalk_common.cc
            ${BREAKPAD_SRC}/processor/stackwalker.cc
            ${BREAKPAD_SRC}/processor/stackwalker_amd64.cc
            ${BREAKPAD_SRC}/processor/stackwalker_arm.cc
            ${BREAKPAD_SRC}/processor/stackwalker_arm64.cc
            ${BREAKPAD_SRC}/processor/stackwalker_address_list.cc
            ${BREAKPAD_SRC}/processor/stackwalker_mips.cc
            ${BREAKPAD_SRC}/processor/stackwalker_ppc.cc
            ${BREAKPAD_SRC}/processor/stackwalker_ppc64.cc
            ${BREAKPAD_SRC}/processor/stackwalker_riscv.cc
            ${BREAKPAD_SRC}/processor/stackwalker_riscv64.cc
            ${BREAKPAD_SRC}/processor/stackwalker_sparc.cc
            ${BREAKPAD_SRC}/processor/stackwalker_x86.cc
            ${BREAKPAD_SRC}/processor/symbolic_constants_win.cc
            ${BREAKPAD_SRC}/processor/tokenize.cc

            ${BREAKPAD_SRC}/client/windows/crash_generation/client_info.cc
            ${BREAKPAD_SRC}/client/windows/crash_generation/crash_generation_client.cc
            ${BREAKPAD_SRC}/client/windows/crash_generation/crash_generation_server.cc
            ${BREAKPAD_SRC}/client/windows/crash_generation/minidump_generator.cc
            ${BREAKPAD_SRC}/client/windows/handler/exception_handler.cc
            ${BREAKPAD_SRC}/common/windows/guid_string.cc
            ${BREAKPAD_SRC}/common/windows/string_utils.cc
            ${BREAKPAD_SRC}/common/convert_UTF.cc
            ${BREAKPAD_SRC}/common/string_conversion.cc

            ${BREAKPAD_SRC}/third_party/libdisasm/ia32_implicit.c
            ${BREAKPAD_SRC}/third_party/libdisasm/ia32_insn.c
            ${BREAKPAD_SRC}/third_party/libdisasm/ia32_invariant.c
            ${BREAKPAD_SRC}/third_party/libdisasm/ia32_modrm.c
            ${BREAKPAD_SRC}/third_party/libdisasm/ia32_opcode_tables.c
            ${BREAKPAD_SRC}/third_party/libdisasm/ia32_operand.c
            ${BREAKPAD_SRC}/third_party/libdisasm/ia32_reg.c
            ${BREAKPAD_SRC}/third_party/libdisasm/ia32_settings.c
            ${BREAKPAD_SRC}/third_party/libdisasm/x86_disasm.c
            ${BREAKPAD_SRC}/third_party/libdisasm/x86_format.c
            ${BREAKPAD_SRC}/third_party/libdisasm/x86_imm.c
            ${BREAKPAD_SRC}/third_party/libdisasm/x86_insn.c
            ${BREAKPAD_SRC}/third_party/libdisasm/x86_misc.c
            ${BREAKPAD_SRC}/third_party/libdisasm/x86_operand_list.c
    )
    target_include_directories(breakpad-win PRIVATE
            ${CMAKE_SOURCE_DIR}/vendor/breakpad-config/windows
            ${BREAKPAD_SRC}
    )
    target_compile_definitions(breakpad-win PRIVATE
            HAVE_CONFIG_H
            # The client is written against the wide Win32 API.
            UNICODE
            _UNICODE
            NOMINMAX
            _CRT_SECURE_NO_WARNINGS
            _CRT_NONSTDC_NO_DEPRECATE
            # minidump_generator.cc still uses stdext::checked_array_iterator,
            # deprecated since VS 2022 17.8 and silenced here on toolsets
            # that still ship it.
            _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING
    )
    target_compile_options(breakpad-win PRIVATE /utf-8 /EHsc /wd4267 /wd4244 /wd4996)
    # VS 2026 (MSVC 14.50+) removed stdext::checked_array_iterator from the
    # STL entirely. Force-include a drop-in replacement into the one file
    # that uses it instead of patching the submodule; the header is a no-op
    # on older toolsets.
    set_source_files_properties(
            ${BREAKPAD_SRC}/client/windows/crash_generation/minidump_generator.cc
            PROPERTIES COMPILE_OPTIONS
            "/FI${CMAKE_SOURCE_DIR}/vendor/breakpad-config/windows/stdext_compat.h"
    )
endif ()

add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES} ${BREAKPAD_SOURCE_FILES})
target_compile_definitions(source2mod-sdk PUBLIC SOURCE2MOD_CORE)

if (MSVC)
    # Windows.h reaches the core through the SDK and pluginmanager.h; without
    # this its min/max macros break every std::min/std::max after it.
    target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX)
endif ()

if (DEFINED ENV{GITHUB_SHA_SHORT} AND NOT "$ENV{GITHUB_SHA_SHORT}" STREQUAL "")
    add_definitions(-DGITHUB_SHA="$ENV{GITHUB_SHA_SHORT}")
else ()
    add_definitions(-DGITHUB_SHA="Local")
endif ()

if (DEFINED ENV{SEMVER} AND NOT "$ENV{SEMVER}" STREQUAL "")
    add_definitions(-DSEMVER="$ENV{SEMVER}")
else ()
    add_definitions(-DSEMVER="Local")
endif ()

include(${CMAKE_CURRENT_LIST_DIR}/makefiles/metamod/configure_metamod.cmake)

find_package(Git QUIET)
if (GIT_FOUND)
    execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_SHA
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
    )
else ()
    set(GIT_SHA "nogit")
endif ()

if (UNIX AND NOT APPLE)
    SET(source2mod_include_directories
            ${CMAKE_SOURCE_DIR}/src
            ${METAMOD_DIR}/core
            ${METAMOD_DIR}/third_party/khook/include
            ${CMAKE_SOURCE_DIR}/vendor
            ${CMAKE_SOURCE_DIR}/vendor/dynlibutils/include
            ${CMAKE_SOURCE_DIR}/vendor/dynlibutils/include/dynlibutils
            ${CMAKE_SOURCE_DIR}/vendor/openssl/include
            ${CMAKE_SOURCE_DIR}/vendor/mysql/include
            ${CMAKE_SOURCE_DIR}/vendor/spdlog/include
            # Breakpad, for the crash handler. configs.h in breakpad-config is
            # the autoconf output the sources expect. Only the src root goes
            # in: its subdirectories carry a module.h and a logging.h that
            # would shadow Source2Mod's and tier0's for anyone including
            # them by bare name.
            ${CMAKE_SOURCE_DIR}/vendor/breakpad-config/linux
            ${CMAKE_SOURCE_DIR}/vendor/breakpad/src
    )
    SET(source2mod_link_libraries
            ${CMAKE_SOURCE_DIR}/vendor/mysql/lib/libmysqlclient.a
            ${CMAKE_SOURCE_DIR}/vendor/openssl/lib/linux/libssl.a
            ${CMAKE_SOURCE_DIR}/vendor/openssl/lib/linux/libcrypto.a
            ${CMAKE_SOURCE_DIR}/vendor/breakpad-build/libbreakpad.a
            ${CMAKE_SOURCE_DIR}/vendor/breakpad-build/libbreakpad-client.a
            ${CMAKE_SOURCE_DIR}/vendor/breakpad-build/libdisasm.a
            dynlibutils
            z
    )
    set_target_properties(${PROJECT_NAME} PROPERTIES
            PREFIX ""
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/addons/source2mod/bin/linuxsteamrt64"
    )
elseif (WIN32)
    SET(source2mod_include_directories
            ${CMAKE_SOURCE_DIR}/src
            ${METAMOD_DIR}/core
            ${METAMOD_DIR}/third_party/khook/include
            ${CMAKE_SOURCE_DIR}/vendor
            ${CMAKE_SOURCE_DIR}/vendor/dynlibutils/include
            ${CMAKE_SOURCE_DIR}/vendor/dynlibutils/include/dynlibutils
            ${CMAKE_SOURCE_DIR}/vendor/openssl/include
            ${CMAKE_SOURCE_DIR}/vendor/mysql/include
            ${CMAKE_SOURCE_DIR}/vendor/spdlog/include
            # Breakpad, for the crash handler -- see the Linux branch for why
            # only the src root goes in.
            ${CMAKE_SOURCE_DIR}/vendor/breakpad-config/windows
            ${CMAKE_SOURCE_DIR}/vendor/breakpad/src
    )
    SET(source2mod_link_libraries
            ${CMAKE_SOURCE_DIR}/vendor/mysql/lib/mysqlclient.lib
            ${CMAKE_SOURCE_DIR}/vendor/openssl/lib/windows/ssl.lib
            ${CMAKE_SOURCE_DIR}/vendor/openssl/lib/windows/crypto.lib
            crypt32.lib
            # Crash handler: breakpad built above, DbgHelp for symbolizing
            # addons/ modules from their PDBs.
            breakpad-win
            dbghelp.lib
            dynlibutils
    )
    set_target_properties(${PROJECT_NAME} PROPERTIES
            PREFIX ""
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/addons/source2mod/bin/win64"
            # A DLL is a RUNTIME artifact on Windows, LIBRARY only places the import .lib.
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/addons/source2mod/bin/win64"
    )
endif ()

target_include_directories(${PROJECT_NAME} PUBLIC ${source2mod_include_directories})
target_include_directories(source2mod-sdk PUBLIC ${source2mod_include_directories})

include(${CMAKE_CURRENT_LIST_DIR}/makefiles/sourcepawn/sourcepawn.cmake)

target_link_libraries(${PROJECT_NAME}
        source2mod-config
        source2mod-sdk
        sp-vm
        ${source2mod_link_libraries}
)

# breakpad_types.h insists on this being defined before <inttypes.h> is
# first seen, which with the SDK's headers is long before it gets a say.
target_compile_definitions(${PROJECT_NAME} PRIVATE __STDC_FORMAT_MACROS)

if (UNIX AND NOT APPLE)
    # Breakpad is an implementation detail of the core. Kept out of the .so's
    # export table so another crash handler loaded next to Source2Mod
    # (Accelerator, AcceleratorLocal) cannot bind to this copy of it.
    target_link_options(${PROJECT_NAME} PRIVATE
            -Wl,--exclude-libs=libbreakpad.a:libbreakpad-client.a:libdisasm.a:libsp-vm.a:libsp-zlib.a
    )
endif ()

add_custom_command(
        TARGET ${PROJECT_NAME} PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/configs ${CMAKE_BINARY_DIR}
)
