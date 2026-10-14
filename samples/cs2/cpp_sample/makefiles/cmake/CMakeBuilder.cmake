# The plugin binary -- CMake twin of makefiles/ambuild/AMBuilder.
# Included by CMakeLists.txt after shared.cmake and the SDK.

# The plugin's own sources -- the same list, in the same order, as
# makefiles/ambuild/AMBuilder. Listed, not globbed: a new .cpp goes into both.
set(SOURCE_FILES
    src/plugin.cpp
    src/plugin.h
)

# A shared library named <name>.smx, linked with the SDK (and through it s2sdk
# and the game protobufs).
add_source2mod_plugin(${PROJECT_NAME} ${SOURCE_FILES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${VERSION_GEN_DIR}
)

# What the platform file asks for on the link line (the export map on Linux),
# like linux.ambuild / windows.ambuild's ConfigureBinary() on the AMBuild side.
# Collected here and applied in package.cmake, after the platform file ran.
set(PLUGIN_LINK_OPTIONS "")
set(PLUGIN_LINK_DEPENDS "")
