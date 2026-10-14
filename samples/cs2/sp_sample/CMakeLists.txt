cmake_minimum_required(VERSION 3.18)
project(sp_sample NONE)

# The compiler and the include set come from a Source2Mod build or install:
# SOURCE2MOD_SCRIPTING is its addons/source2mod/scripting (spcomp + include/),
# the counterpart of cpp_sample's SOURCE2MOD_SDK.
if (DEFINED ENV{SOURCE2MOD_SCRIPTING} AND NOT "$ENV{SOURCE2MOD_SCRIPTING}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{SOURCE2MOD_SCRIPTING}" SOURCE2MOD_SCRIPTING)
else ()
    message(FATAL_ERROR "SOURCE2MOD_SCRIPTING env variable not set (a Source2Mod build's addons/source2mod/scripting)")
endif ()

# Settings every platform shares, version_gen.inc (makefiles/ambuild/shared.ambuild
# on the AMBuild side).
include(${CMAKE_SOURCE_DIR}/makefiles/cmake/shared.cmake)

if (LINUX)
    include(${CMAKE_SOURCE_DIR}/makefiles/cmake/linux.cmake)
elseif (WIN32)
    include(${CMAKE_SOURCE_DIR}/makefiles/cmake/windows.cmake)
endif ()

# The plugin binary (makefiles/ambuild/AMBuilder on the AMBuild side).
include(${CMAKE_SOURCE_DIR}/makefiles/cmake/CMakeBuilder.cmake)

# Install layout (makefiles/ambuild/package.ambuild on the AMBuild side).
include(${CMAKE_SOURCE_DIR}/makefiles/cmake/package.cmake)
