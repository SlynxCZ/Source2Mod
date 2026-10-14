# Windows (MSVC) settings -- CMake twin of makefiles/ambuild/windows.ambuild.

# The static CRT is set at the top of CMakeLists.txt (before the SDK is added,
# so both agree); COMPILER_MSVC*, WIN32, WIN64 and the SDK's libraries come
# with the SDK target, through s2sdk-manifests' windows.cmake. Nothing
# Windows-only for the plugin itself.
