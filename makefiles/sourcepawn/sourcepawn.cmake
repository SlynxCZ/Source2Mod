# SourcePawn (vendor/sourcepawn, AlliedModders): the VM -- JIT on x86-64 --
# is linked into the core, which runs .smx plugins with it; spcomp is built
# next to it into addons/source2mod/scripting so plugins compile against the
# include set shipped there. The sources lists follow sourcepawn's AMBuilders.

set(SP_ROOT ${CMAKE_SOURCE_DIR}/vendor/sourcepawn)
set(SP_AMTL ${SP_ROOT}/third_party/amtl)

option(SOURCE2MOD_BUILD_SPCOMP "Build spcomp next to the core" ON)

function(sourcepawn_setup target)
    set_target_properties(${target} PROPERTIES
            CXX_STANDARD 17
            POSITION_INDEPENDENT_CODE ON
            CXX_VISIBILITY_PRESET hidden
            C_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON
    )
    target_include_directories(${target} PRIVATE
            ${SP_ROOT}
            ${SP_ROOT}/include
            ${SP_ROOT}/third_party
            ${SP_ROOT}/vm
            ${SP_AMTL}
    )
    target_compile_definitions(${target} PRIVATE KE_THREADSAFE SP_HAS_JIT)
    if (MSVC)
        target_compile_definitions(${target} PRIVATE WIN32 _WINDOWS NOMINMAX _CRT_SECURE_NO_WARNINGS)
    else ()
        target_compile_definitions(${target} PRIVATE _GNU_SOURCE)
        target_compile_options(${target} PRIVATE
                -Wno-invalid-offsetof
                -Wno-delete-non-virtual-dtor
                -Wno-delete-non-abstract-non-virtual-dtor
                -Wno-unused-private-field
                -Wno-implicit-exception-spec-mismatch
                -Wno-unknown-warning-option
                -fno-strict-aliasing
        )
    endif ()
endfunction()

add_library(sp-zlib STATIC
        ${SP_ROOT}/third_party/zlib/adler32.c
        ${SP_ROOT}/third_party/zlib/compress.c
        ${SP_ROOT}/third_party/zlib/crc32.c
        ${SP_ROOT}/third_party/zlib/deflate.c
        ${SP_ROOT}/third_party/zlib/gzclose.c
        ${SP_ROOT}/third_party/zlib/gzlib.c
        ${SP_ROOT}/third_party/zlib/gzread.c
        ${SP_ROOT}/third_party/zlib/gzwrite.c
        ${SP_ROOT}/third_party/zlib/infback.c
        ${SP_ROOT}/third_party/zlib/inffast.c
        ${SP_ROOT}/third_party/zlib/inflate.c
        ${SP_ROOT}/third_party/zlib/inftrees.c
        ${SP_ROOT}/third_party/zlib/trees.c
        ${SP_ROOT}/third_party/zlib/uncompr.c
        ${SP_ROOT}/third_party/zlib/zutil.c
)
sourcepawn_setup(sp-zlib)
if (NOT MSVC)
    target_compile_options(sp-zlib PRIVATE -Wno-implicit-function-declaration -Wno-deprecated-non-prototype)
endif ()

set(SP_VM_SOURCES
        api.cpp base-runtime.cpp builtins.cpp code-allocator.cpp code-stubs.cpp
        control-flow.cpp compiled-function.cpp debugging.cpp debug-metadata.cpp
        environment.cpp file-utils.cpp graph-builder.cpp interpreter.cpp md5/md5.cpp
        method-info.cpp method-verifier.cpp opcodes.cpp plugin-runtime.cpp rtti.cpp
        runtime-helpers.cpp scripted-invoker.cpp smx-image.cpp stack-frames.cpp
        watchdog_timer.cpp
        # SP_HAS_JIT, x86-64
        jit.cpp linking.cpp
        x64/assembler-x64.cpp x64/code-stubs-x64.cpp x64/features-x64.cpp
        x64/macro-assembler-x64.cpp x64/jit_x64.cpp
)
list(TRANSFORM SP_VM_SOURCES PREPEND ${SP_ROOT}/vm/)

add_library(sp-vm STATIC ${SP_VM_SOURCES} ${SP_ROOT}/utils/pool-allocator.cpp)
sourcepawn_setup(sp-vm)
target_link_libraries(sp-vm PRIVATE sp-zlib)
# What the core includes to embed the VM (sp_vm_api.h and the environment).
target_include_directories(sp-vm INTERFACE
        ${SP_ROOT}/include
        ${SP_ROOT}/vm
        ${SP_ROOT}
        ${SP_AMTL}
)
target_compile_definitions(sp-vm INTERFACE KE_THREADSAFE SP_HAS_JIT)

if (SOURCE2MOD_BUILD_SPCOMP)
    # spcomp wants <sourcemod_version.h>, which sourcepawn's build generates
    # from product.version.
    file(READ ${SP_ROOT}/product.version SP_PRODUCT_VERSION)
    string(STRIP "${SP_PRODUCT_VERSION}" SP_PRODUCT_VERSION)
    string(REPLACE "." "," SP_VERSION_FILE "${SP_PRODUCT_VERSION},0,0")
    file(WRITE ${CMAKE_BINARY_DIR}/sp-generated/sourcemod_version.h
            "#define SM_VERSION_FILE ${SP_VERSION_FILE}\n#define SM_VERSION_STRING \"${SP_PRODUCT_VERSION}\"\n")

    set(SP_COMPILER_SOURCES
            assembler.cpp array-helpers.cpp ast-printer.cpp builtin-generator.cpp
            coercion-rules.cpp compile-context.cpp code-generator.cpp data-queue.cpp
            errors.cpp expressions.cpp lexer.cpp main.cpp name-resolution.cpp
            parse-node.cpp parser.cpp pool-objects.cpp rtti-builder.cpp sci18n.cpp
            sctracker.cpp semantics.cpp scopes.cpp source-file.cpp source-manager.cpp
            symbols.cpp type-checker.cpp types.cpp driver.cpp
    )
    list(TRANSFORM SP_COMPILER_SOURCES PREPEND ${SP_ROOT}/compiler/)

    add_executable(spcomp
            ${SP_COMPILER_SOURCES}
            ${SP_ROOT}/libsmx/data-pool.cpp
            ${SP_ROOT}/libsmx/smx-builder.cpp
            ${SP_ROOT}/utils/pool-allocator.cpp
            ${SP_AMTL}/amtl/experimental/am-argparser.cpp
    )
    sourcepawn_setup(spcomp)
    target_include_directories(spcomp PRIVATE ${CMAKE_BINARY_DIR}/sp-generated ${SP_ROOT}/compiler)
    # The compiler verifies what it wrote by loading it into the VM.
    target_link_libraries(spcomp PRIVATE sp-vm sp-zlib)
    if (NOT MSVC)
        target_link_libraries(spcomp PRIVATE pthread)
    endif ()
    set_target_properties(spcomp PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/addons/source2mod/scripting
    )
    add_dependencies(${PROJECT_NAME} spcomp)
endif ()
