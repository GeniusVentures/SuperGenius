addtest(transaction_sync_test
transaction_sync_test.cpp
    )

target_include_directories(transaction_sync_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(transaction_sync_test
    genius_node_test
    json_secure_storage
)

if(WIN32)
    target_link_options(transaction_sync_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(transaction_sync_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(transaction_sync_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

addtest(genius_node_bootstrap_reconnect_test
    genius_node_bootstrap_reconnect_test.cpp
)

target_include_directories(genius_node_bootstrap_reconnect_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(genius_node_bootstrap_reconnect_test
    genius_node_test
    json_secure_storage
)

if(WIN32)
    target_link_options(genius_node_bootstrap_reconnect_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(genius_node_bootstrap_reconnect_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(genius_node_bootstrap_reconnect_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

addtest(migration_sync_test
    migration_sync_test.cpp
)

target_link_libraries(migration_sync_test
    genius_node_test
    json_secure_storage
)

if(WIN32)
    target_link_options(migration_sync_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(migration_sync_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(migration_sync_test PUBLIC
            "-Wl,--whole-archive"
            "$<TARGET_FILE:genius_node_test>"
            "-Wl,--no-whole-archive"
    )
endif()

addtest(transaction_crash_test
    transaction_crash_test.cpp
)

target_link_libraries(transaction_crash_test
    genius_node_test
    json_secure_storage
)

if(WIN32)
    target_link_options(transaction_crash_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(transaction_crash_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(transaction_crash_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

add_custom_command(TARGET migration_sync_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/node10_0_2_0/"
        "$<TARGET_FILE_DIR:migration_sync_test>/node10_0_2_0"
        COMMENT "Copying node10_0_2_0 data"
)

add_custom_command(TARGET migration_sync_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/node20_0_2_0/"
        "$<TARGET_FILE_DIR:migration_sync_test>/node20_0_2_0"
        COMMENT "Copying node20_0_2_0 data"
)

# Several suites in this directory boot multiple full nodes; running them
# concurrently starves CPU on smaller CI runners (observed: a genesis creator
# starved for 30s while eight test binaries ran), stalling startup past any
# sane timeout. Sharing this lock serializes them against the other
# node-booting suites while light suites keep running in parallel.
set_tests_properties(genius_node_bootstrap_reconnect_test migration_sync_test transaction_crash_test transaction_sync_test
    PROPERTIES RESOURCE_LOCK genius_node_pool)
