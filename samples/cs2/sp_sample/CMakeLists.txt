cmake_minimum_required(VERSION 3.18)
project(sp_sample NONE)

# The SDK is resolved from the environment, the same way cpp_sample resolves
# it: SOURCE2MOD_SDK. Its SdkScripting.cmake gives the include set
# (SOURCE2MOD_SCRIPTING_INCLUDE) and finds spcomp (SOURCE2MOD_SPCOMP: SPCOMP,
# else the PATH).
if (DEFINED ENV{SOURCE2MOD_SDK} AND NOT "$ENV{SOURCE2MOD_SDK}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{SOURCE2MOD_SDK}" SOURCE2MOD_SDK)
else ()
    message(FATAL_ERROR "SOURCE2MOD_SDK env variable not set")
endif ()

include(${SOURCE2MOD_SDK}/SdkScripting.cmake)

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
