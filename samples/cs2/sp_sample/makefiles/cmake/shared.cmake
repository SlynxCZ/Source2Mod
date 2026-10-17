# Settings every platform shares -- CMake twin of makefiles/ambuild/shared.ambuild.
# The per-platform ones are in linux.cmake / windows.cmake.

if (UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif ()

# plugin-metadata.json is the one place the plugin's name, author and version
# live. tools/version_gen.py turns it into version_gen.inc, and the AMBuild build
# calls the same script, so the two cannot drift.
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(VERSION_GEN_DIR "${CMAKE_BINARY_DIR}/versioning")

execute_process(
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tools/version_gen.py
                --metadata ${CMAKE_SOURCE_DIR}/plugin-metadata.json
                --output ${VERSION_GEN_DIR}/version_gen.inc
        RESULT_VARIABLE VERSION_GEN_RESULT
)

if (NOT VERSION_GEN_RESULT EQUAL 0)
    message(FATAL_ERROR "version_gen.py failed (${VERSION_GEN_RESULT})")
endif ()

# Re-run cmake when the metadata changes.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/plugin-metadata.json)

# Where the compiler looks: Source2Mod's include set, cpp_sample's .inc (its
# section 12 natives), version_gen.inc.
set(SPCOMP_INCLUDE_DIRS
    ${SOURCE2MOD_SCRIPTING_INCLUDE}
    ${CMAKE_SOURCE_DIR}/../cpp_sample/scripting/include
    ${VERSION_GEN_DIR}
)
