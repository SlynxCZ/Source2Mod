cmake_minimum_required(VERSION 3.18)
project(cpp_sample CXX)

# The SDK library is built against the static CRT (/MT); a plugin linking the
# dynamic one gets unresolved CRT internals (_recalloc_base from the s2sdk's
# memoverride.cpp). Set before the SDK is added, so both agree.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# The SDK is resolved from the environment, the same way Source2Mod's core
# resolves it -- one checkout for Source2Mod and everything built against it.
if (DEFINED ENV{SOURCE2MOD_SDK} AND NOT "$ENV{SOURCE2MOD_SDK}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{SOURCE2MOD_SDK}" SOURCE2MOD_SDK)
else ()
    message(FATAL_ERROR "SOURCE2MOD_SDK env variable not set")
endif ()

# Settings every platform shares, version_gen.h (makefiles/ambuild/shared.ambuild
# on the AMBuild side).
include(${CMAKE_SOURCE_DIR}/makefiles/cmake/shared.cmake)

# The SDK: s2sdk through s2sdk-manifests, the SDK's own sources, and
# add_source2mod_plugin().
add_subdirectory(${SOURCE2MOD_SDK} ${CMAKE_BINARY_DIR}/source2mod-sdk)

# The plugin binary (makefiles/ambuild/AMBuilder on the AMBuild side).
include(${CMAKE_SOURCE_DIR}/makefiles/cmake/CMakeBuilder.cmake)

if (LINUX)
    include(${CMAKE_SOURCE_DIR}/makefiles/cmake/linux.cmake)
elseif (WIN32)
    include(${CMAKE_SOURCE_DIR}/makefiles/cmake/windows.cmake)
endif ()

# Install layout (makefiles/ambuild/package.ambuild on the AMBuild side).
include(${CMAKE_SOURCE_DIR}/makefiles/cmake/package.cmake)
