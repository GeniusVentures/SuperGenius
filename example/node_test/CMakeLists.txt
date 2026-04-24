add_executable(node_example
    NodeExample.cpp
)
include_directories(
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(node_example PRIVATE
    genius_node
)
target_include_directories(node_example PRIVATE ${AsyncIOManager_INCLUDE_DIR})

option(NODE_EXAMPLE_ENABLE_SENTRY "Enable optional Sentry-native integration for node_example" OFF)
set(node_example_with_sentry FALSE)
if(NODE_EXAMPLE_ENABLE_SENTRY)
    find_package(sentry CONFIG QUIET)
    if(TARGET sentry::sentry)
        target_link_libraries(node_example PRIVATE sentry::sentry)
        target_compile_definitions(node_example PRIVATE SG_NODE_EXAMPLE_WITH_SENTRY=1)
        set(node_example_with_sentry TRUE)
    elseif(TARGET sentry)
        target_link_libraries(node_example PRIVATE sentry)
        target_compile_definitions(node_example PRIVATE SG_NODE_EXAMPLE_WITH_SENTRY=1)
        set(node_example_with_sentry TRUE)
    else()
        message(WARNING "NODE_EXAMPLE_ENABLE_SENTRY=ON but sentry package target was not found; building without Sentry integration")
    endif()
endif()

if(node_example_with_sentry AND CMAKE_OBJCOPY)
    add_custom_command(
        TARGET node_example
        POST_BUILD
        WORKING_DIRECTORY $<TARGET_FILE_DIR:node_example>
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:node_example>
        COMMAND ${CMAKE_OBJCOPY} --only-keep-debug $<TARGET_FILE_NAME:node_example> $<TARGET_FILE_BASE_NAME:node_example>.debug
        COMMAND ${CMAKE_OBJCOPY} --strip-debug $<TARGET_FILE_NAME:node_example>
        COMMAND ${CMAKE_OBJCOPY} --add-gnu-debuglink=$<TARGET_FILE_BASE_NAME:node_example>.debug $<TARGET_FILE_NAME:node_example>
        COMMENT "Generating node_example debug symbol file for Sentry upload"
        VERBATIM
    )
elseif(node_example_with_sentry)
    message(WARNING "Sentry is enabled for node_example but CMAKE_OBJCOPY is unavailable; .debug file will not be generated")
endif()

if(WIN32)
    target_link_options(node_example PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node>)

elseif(APPLE)
    target_link_options(node_example PUBLIC -force_load "$<TARGET_FILE:genius_node>")

else()
    target_link_options(node_example PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node>"
        "-Wl,--no-whole-archive"
    )
endif()
