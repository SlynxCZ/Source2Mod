# The install layout -- CMake twin of makefiles/ambuild/package.ambuild: the
# plugin as addons/source2mod/plugins/<name>.smx, and configs/ (when there is
# one) copied over the build root.

set(PLUGIN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/addons/source2mod/plugins")

set_target_properties(${PROJECT_NAME} PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${PLUGIN_OUTPUT_DIR}
    # The .dll on Windows is a RUNTIME artifact.
    RUNTIME_OUTPUT_DIRECTORY ${PLUGIN_OUTPUT_DIR}
)

if (PLUGIN_LINK_OPTIONS)
    target_link_options(${PROJECT_NAME} PRIVATE ${PLUGIN_LINK_OPTIONS})
endif ()
if (PLUGIN_LINK_DEPENDS)
    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS ${PLUGIN_LINK_DEPENDS})
endif ()

if (EXISTS ${CMAKE_SOURCE_DIR}/configs)
    add_custom_command(
        TARGET ${PROJECT_NAME} PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/configs ${CMAKE_BINARY_DIR}
    )
endif ()
