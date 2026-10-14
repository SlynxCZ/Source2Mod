# The plugin binary -- CMake twin of makefiles/ambuild/AMBuilder.
# Included by CMakeLists.txt after shared.cmake and the platform file.

if (NOT EXISTS ${SPCOMP_EXECUTABLE})
    message(FATAL_ERROR "spcomp not found at ${SPCOMP_EXECUTABLE}")
endif ()

# The script and what it includes -- the same lists as makefiles/ambuild/AMBuilder.
set(SCRIPT_SOURCE ${CMAKE_SOURCE_DIR}/scripting/sp_sample.sp)
set(SCRIPT_OUTPUT ${CMAKE_BINARY_DIR}/sp_sample.smx)

set(SPCOMP_ARGS "")
foreach (dir ${SPCOMP_INCLUDE_DIRS})
    list(APPEND SPCOMP_ARGS "-i${dir}")
endforeach ()

file(GLOB_RECURSE SCRIPT_DEPENDS
    ${SOURCE2MOD_SCRIPTING}/include/*.inc
    ${CMAKE_SOURCE_DIR}/../cpp_sample/scripting/include/*.inc
)

add_custom_command(
    OUTPUT ${SCRIPT_OUTPUT}
    COMMAND ${SPCOMP_EXECUTABLE} ${SCRIPT_SOURCE} ${SPCOMP_ARGS} -o${SCRIPT_OUTPUT}
    DEPENDS ${SCRIPT_SOURCE} ${SCRIPT_DEPENDS} ${VERSION_GEN_DIR}/version_gen.inc
    COMMENT "spcomp sp_sample.sp"
    VERBATIM
)

add_custom_target(${PROJECT_NAME} ALL DEPENDS ${SCRIPT_OUTPUT})
