# Linux settings -- CMake twin of makefiles/ambuild/linux.ambuild.

# The SDK's Linux defines (_LINUX, POSIX, stricmp=strcasecmp, ...), flags and
# libraries come with the SDK target, through s2sdk-manifests' linux.cmake.

# Export only CreateInterface -- the same version script AMBuild links with.
# Source2Mod finds the plugin through it, and nothing else should leak out.
set(PLUGIN_EXPORT_MAP ${CMAKE_SOURCE_DIR}/makefiles/ExportMap)
list(APPEND PLUGIN_LINK_OPTIONS "-Wl,--version-script=${PLUGIN_EXPORT_MAP}")
list(APPEND PLUGIN_LINK_DEPENDS ${PLUGIN_EXPORT_MAP})
