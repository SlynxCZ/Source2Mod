# The install layout -- CMake twin of makefiles/ambuild/package.ambuild: the
# plugin as addons/source2mod/plugins/<name>.smx, and configs/ (when there is
# one) copied over the build root.

set(PLUGIN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/addons/source2mod/plugins")

add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PLUGIN_OUTPUT_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SCRIPT_OUTPUT} ${PLUGIN_OUTPUT_DIR}/${PROJECT_NAME}.smx
)

if (EXISTS ${CMAKE_SOURCE_DIR}/configs)
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/configs ${CMAKE_BINARY_DIR}
    )
endif ()
