# Settings every platform shares -- CMake twin of makefiles/ambuild/shared.ambuild.
# The per-platform ones are in linux.cmake / windows.cmake; the compiler flags
# themselves come with the SDK target (add_source2mod_plugin links it).

if (UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif ()

if (WIN32 AND NOT MSVC)
    message(FATAL_ERROR "MSVC restricted.")
endif ()

set(CMAKE_CXX_STANDARD 20)

# plugin-metadata.json is the one place the plugin's name, author and version
# live. tools/version_gen.py turns it into version_gen.h, and the AMBuild build
# calls the same script, so the two cannot drift.
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(VERSION_GEN_DIR "${CMAKE_BINARY_DIR}/versioning")

execute_process(
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tools/version_gen.py
                --metadata ${CMAKE_SOURCE_DIR}/plugin-metadata.json
                --output ${VERSION_GEN_DIR}/version_gen.h
        RESULT_VARIABLE VERSION_GEN_RESULT
)

if (NOT VERSION_GEN_RESULT EQUAL 0)
    message(FATAL_ERROR "version_gen.py failed (${VERSION_GEN_RESULT})")
endif ()

# Re-run cmake when the metadata changes.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/plugin-metadata.json)
