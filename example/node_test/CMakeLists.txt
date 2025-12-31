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
