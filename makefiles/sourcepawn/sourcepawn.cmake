# SourcePawn (vendor/sourcepawn, Source2Mod's fork of AlliedModders'): the VM
# -- both its v1 (legacy) and v2 runtimes, JIT on x86-64 -- is linked into the
# core, which runs .smx plugins with it; spcomp is built next to it into
# addons/source2mod/scripting so plugins compile against the include set
# shipped there. The source lists and settings follow sourcepawn's
# AMBuildScript and AMBuilders (libsourcepawn_static = libsmx + utils + vm;
# spcomp = compiler + driver.cpp; both on zlib, libamtl, mimalloc, capstone).

set(SP_ROOT ${CMAKE_SOURCE_DIR}/vendor/sourcepawn)
set(SP_AMTL ${SP_ROOT}/third_party/amtl)
set(SP_THIRD ${SP_ROOT}/third_party)

option(SOURCE2MOD_BUILD_SPCOMP "Build spcomp next to the core" ON)

file(READ ${SP_ROOT}/product.version SP_PRODUCT_VERSION)
string(STRIP "${SP_PRODUCT_VERSION}" SP_PRODUCT_VERSION)

# What every SourcePawn target gets (AMBuildScript: configure_gcc/msvc,
# SetupBinForArch); AddVmFlags on top for the VM and spcomp.
function(sourcepawn_setup target)
    set_target_properties(${target} PROPERTIES
            CXX_STANDARD 20
            CXX_STANDARD_REQUIRED ON
            POSITION_INDEPENDENT_CODE ON
            CXX_VISIBILITY_PRESET hidden
            C_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON
    )
    target_include_directories(${target} PRIVATE
            ${SP_ROOT}
            ${SP_ROOT}/include
            ${SP_ROOT}/vm
            ${SP_THIRD}
            ${SP_THIRD}/mimalloc/include
            ${SP_THIRD}/capstone/include
            ${SP_AMTL}
    )
    target_compile_definitions(${target} PRIVATE KE_THREADSAFE)
    if (MSVC)
        target_compile_definitions(${target} PRIVATE WIN32 _WINDOWS NOMINMAX _CRT_SECURE_NO_WARNINGS
                _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)
    else ()
        # The core's std::string ABI (s2sdk builds with the old one): the
        # VM's C++ API (SmxImage::GetPstructValue, ...) passes std::string.
        target_compile_definitions(${target} PRIVATE _GNU_SOURCE _GLIBCXX_USE_CXX11_ABI=0)
        target_compile_options(${target} PRIVATE
                $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
                -Wno-invalid-offsetof
                -Wno-delete-non-virtual-dtor
                -Wno-delete-non-abstract-non-virtual-dtor
                -Wno-non-virtual-dtor
                -Wno-overloaded-virtual
                -Wno-unused-private-field
                -Wno-implicit-exception-spec-mismatch
                -Wno-unknown-warning-option
                -Wno-switch
                -fno-strict-aliasing
        )
    endif ()
endfunction()

function(sourcepawn_vm_flags target)
    target_compile_definitions(${target} PRIVATE
            SP_ARCH_STR="x64"
            SOURCEPAWN_VERSION="SourcePawn ${SP_PRODUCT_VERSION}"
            SP_JIT_V1
            SP_JIT_V2
    )
endfunction()

# ---- third party --------------------------------------------------------------

add_library(sp-zlib STATIC
        ${SP_THIRD}/zlib/adler32.c ${SP_THIRD}/zlib/compress.c ${SP_THIRD}/zlib/crc32.c
        ${SP_THIRD}/zlib/deflate.c ${SP_THIRD}/zlib/gzclose.c ${SP_THIRD}/zlib/gzlib.c
        ${SP_THIRD}/zlib/gzread.c ${SP_THIRD}/zlib/gzwrite.c ${SP_THIRD}/zlib/infback.c
        ${SP_THIRD}/zlib/inffast.c ${SP_THIRD}/zlib/inflate.c ${SP_THIRD}/zlib/inftrees.c
        ${SP_THIRD}/zlib/trees.c ${SP_THIRD}/zlib/uncompr.c ${SP_THIRD}/zlib/zutil.c
)
sourcepawn_setup(sp-zlib)
if (NOT MSVC)
    target_compile_options(sp-zlib PRIVATE -w)
endif ()

set(SP_MIMALLOC_SOURCES
        alloc.c alloc-aligned.c alloc-posix.c arena.c arena-meta.c bitmap.c heap.c init.c
        libc.c options.c os.c page.c page-map.c random.c stats.c theap.c threadlocal.c
        prim/prim.c
)
list(TRANSFORM SP_MIMALLOC_SOURCES PREPEND ${SP_THIRD}/mimalloc/src/)
add_library(sp-mimalloc STATIC ${SP_MIMALLOC_SOURCES})
sourcepawn_setup(sp-mimalloc)
target_include_directories(sp-mimalloc PRIVATE ${SP_THIRD}/mimalloc/src)
target_compile_definitions(sp-mimalloc PRIVATE MI_STATIC_LIB)
target_compile_options(sp-mimalloc PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/w,-w>)

set(SP_CAPSTONE_SOURCES
        cs.c Mapping.c MCInst.c MCInstrDesc.c MCRegisterInfo.c SStream.c utils.c
        arch/AArch64/AArch64BaseInfo.c arch/AArch64/AArch64Disassembler.c
        arch/AArch64/AArch64InstPrinter.c arch/AArch64/AArch64Mapping.c arch/AArch64/AArch64Module.c
        arch/X86/X86Disassembler.c arch/X86/X86DisassemblerDecoder.c arch/X86/X86IntelInstPrinter.c
        arch/X86/X86InstPrinterCommon.c arch/X86/X86Mapping.c arch/X86/X86Module.c
        arch/X86/X86ATTInstPrinter.c
)
list(TRANSFORM SP_CAPSTONE_SOURCES PREPEND ${SP_THIRD}/capstone/)
add_library(sp-capstone STATIC ${SP_CAPSTONE_SOURCES})
sourcepawn_setup(sp-capstone)
target_include_directories(sp-capstone PRIVATE ${SP_THIRD}/capstone)
target_compile_definitions(sp-capstone PRIVATE CAPSTONE_HAS_X86 CAPSTONE_HAS_ARM64 CAPSTONE_USE_SYS_DYN_MEM)
target_compile_options(sp-capstone PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/w,-w>)

add_library(sp-amtl STATIC ${SP_AMTL}/amtl/experimental/am-argparser.cpp)
sourcepawn_setup(sp-amtl)

# ---- the VM (libsourcepawn_static) ------------------------------------------------

set(SP_VM_SOURCES
        api.cpp base-runtime.cpp code-allocator.cpp code-stubs.cpp compiled-function.cpp
        debugging.cpp debug-metadata.cpp environment.cpp file-utils.cpp heap.cpp md5/md5.cpp
        objects.cpp rtti.cpp smx-image.cpp stack-frames.cpp type-cache.cpp watchdog_timer.cpp
        virtmem-64bit.cpp
        # v1 (legacy)
        legacy/builtins.cpp legacy/control-flow.cpp legacy/graph-builder.cpp legacy/interpreter.cpp
        legacy/method-info.cpp legacy/method-verifier.cpp legacy/opcodes.cpp legacy/plugin-runtime.cpp
        legacy/runtime-helpers.cpp legacy/scripted-invoker.cpp
        # v2
        v2/control-flow.cpp v2/graph-builder.cpp v2/interpreter.cpp v2/lowering/lowering.cpp
        v2/lowering/ll-op.cpp v2/method-info.cpp v2/method-verifier.cpp v2/opcodes.cpp v2/runtime.cpp
        v2/runtime-helpers.cpp v2/scripted-invoker.cpp
        # SP_JIT_V1, x86-64
        legacy/jit.cpp linking.cpp
        legacy/x64/code-stubs-x64.cpp legacy/x64/jit_x64.cpp
        x64/assembler-x64.cpp x64/features-x64.cpp x64/macro-assembler-x64.cpp
        # SP_JIT_V2, x86-64
        v2/jit.cpp v2/x64/code-stubs-x64.cpp v2/x64/jit_x64.cpp
)
if (WIN32)
    list(APPEND SP_VM_SOURCES platform-windows.cpp)
else ()
    list(APPEND SP_VM_SOURCES platform-posix.cpp)
endif ()
list(TRANSFORM SP_VM_SOURCES PREPEND ${SP_ROOT}/vm/)

set(SP_UTILS_SOURCES ${SP_ROOT}/utils/pool-allocator.cpp)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND SP_UTILS_SOURCES ${SP_ROOT}/utils/procmap.cpp)
endif ()

add_library(sp-vm STATIC
        ${SP_VM_SOURCES}
        ${SP_UTILS_SOURCES}
        ${SP_ROOT}/libsmx/data-pool.cpp
        ${SP_ROOT}/libsmx/smx-builder.cpp
)
sourcepawn_setup(sp-vm)
sourcepawn_vm_flags(sp-vm)
target_link_libraries(sp-vm PUBLIC sp-zlib sp-amtl sp-mimalloc sp-capstone)
# What the core includes to embed the VM (sp_vm_api.h and the environment).
target_include_directories(sp-vm INTERFACE
        ${SP_ROOT}/include
        ${SP_ROOT}/vm
        ${SP_ROOT}
        ${SP_THIRD}
        ${SP_THIRD}/mimalloc/include
        ${SP_AMTL}
)
target_compile_definitions(sp-vm INTERFACE KE_THREADSAFE SP_JIT_V1 SP_JIT_V2 SP_ARCH_STR="x64")

if (SOURCE2MOD_BUILD_SPCOMP)
    # spcomp wants <sourcemod_version.h>, which sourcepawn's build generates
    # from product.version.
    string(REPLACE "." "," SP_VERSION_FILE "${SP_PRODUCT_VERSION},0,0")
    file(WRITE ${CMAKE_BINARY_DIR}/sp-generated/sourcemod_version.h
            "#define SM_VERSION_FILE ${SP_VERSION_FILE}\n#define SM_VERSION_STRING \"${SP_PRODUCT_VERSION}\"\n")

    set(SP_COMPILER_SOURCES
            assembler.cpp array-helpers.cpp ast-printer.cpp builtin-generator.cpp
            coercion-rules.cpp compile-context.cpp code-generator.cpp constant-fold.cpp
            data-queue.cpp errors.cpp ir-node.cpp lexer.cpp main.cpp name-resolution.cpp
            parse-node.cpp parser.cpp pool-objects.cpp rtti-builder.cpp sci18n.cpp
            semantics.cpp semantics-types.cpp scopes.cpp source-file.cpp source-manager.cpp
            symbols.cpp types.cpp driver.cpp
    )
    list(TRANSFORM SP_COMPILER_SOURCES PREPEND ${SP_ROOT}/compiler/)

    add_executable(spcomp ${SP_COMPILER_SOURCES})
    sourcepawn_setup(spcomp)
    target_include_directories(spcomp PRIVATE ${CMAKE_BINARY_DIR}/sp-generated ${SP_ROOT}/compiler)
    # The compiler verifies what it wrote by loading it into the VM.
    target_link_libraries(spcomp PRIVATE sp-vm)
    if (NOT MSVC)
        target_link_libraries(spcomp PRIVATE pthread m)
    endif ()
    set_target_properties(spcomp PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/addons/source2mod/scripting
    )
    add_dependencies(${PROJECT_NAME} spcomp)
endif ()

# spshell: sourcepawn's test shell, for reproducing a VM/compiler problem
# outside the game (upstream's tests/ use it). Not part of the core build.
add_executable(spshell EXCLUDE_FROM_ALL ${SP_ROOT}/vm/shell/shell.cpp)
sourcepawn_setup(spshell)
sourcepawn_vm_flags(spshell)
target_include_directories(spshell PRIVATE ${CMAKE_BINARY_DIR}/sp-generated)
target_link_libraries(spshell PRIVATE sp-vm)
if (NOT MSVC)
    target_link_libraries(spshell PRIVATE pthread m dl)
endif ()
