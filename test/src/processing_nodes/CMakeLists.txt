
addtest(processing_nodes_test
    processing_nodes_test.cpp
)

target_include_directories(processing_nodes_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(processing_nodes_test
    genius_node_test
    json_secure_storage
)

# See the note in test/src/account/CMakeLists.txt: the asset path must not depend
# on how deep the binary sits in the build tree.
target_compile_definitions(processing_nodes_test PRIVATE
    SGNS_PROCESSING_ASSETS_DIR="${CMAKE_CURRENT_LIST_DIR}")
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

set_tests_properties(child_tokens_test PROPERTIES TIMEOUT 500)


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

# Several suites in this directory boot multiple full nodes; running them
# concurrently starves CPU on smaller CI runners (observed: a genesis creator
# starved for 30s while eight test binaries ran), stalling startup past any
# sane timeout. Sharing this lock serializes them against the other
# node-booting suites while light suites keep running in parallel.
set_tests_properties(child_tokens_test full_node_test processing_nodes_test
    PROPERTIES RESOURCE_LOCK genius_node_pool)
