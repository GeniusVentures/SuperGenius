
addtest(processing_nodes_test
    processing_nodes_test.cpp
)

target_include_directories(processing_nodes_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(processing_nodes_test
    genius_node_test
    json_secure_storage
)
if(WIN32)
    target_link_options(processing_nodes_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)

elseif(APPLE)
    target_link_options(processing_nodes_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")

else()
    target_link_options(processing_nodes_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

addtest(child_tokens_test
    child_tokens_test.cpp
)

target_include_directories(child_tokens_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(child_tokens_test
    genius_node_test
    json_secure_storage
)
if(WIN32)
    target_link_options(child_tokens_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)

elseif(APPLE)
    target_link_options(child_tokens_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")

else()
    target_link_options(child_tokens_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()


addtest(full_node_test
    full_node_test.cpp
)

target_include_directories(full_node_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(full_node_test
    genius_node_test
    json_secure_storage
)
if(WIN32)
    target_link_options(full_node_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)

elseif(APPLE)
    target_link_options(full_node_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")

else()
    target_link_options(full_node_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()


add_custom_command(TARGET child_tokens_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/data/"
    "$<TARGET_FILE_DIR:child_tokens_test>/child_tokens/data"
    COMMENT "Copying data"
)
add_custom_command(TARGET child_tokens_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/model.mnn"
    "$<TARGET_FILE_DIR:child_tokens_test>/child_tokens/"
    COMMENT "Copying files"
)
