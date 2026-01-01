addtest(transaction_sync_test
transaction_sync_test.cpp
    )

target_include_directories(transaction_sync_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(transaction_sync_test
    genius_node
)

if(WIN32)
    target_link_options(transaction_sync_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node>)
elseif(APPLE)
    target_link_options(transaction_sync_test PUBLIC -force_load "$<TARGET_FILE:genius_node>")
else()
    target_link_options(transaction_sync_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node>"
        "-Wl,--no-whole-archive"
    )
endif()

addtest(migration_sync_test
migration_sync_test.cpp
    )

target_include_directories(migration_sync_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(migration_sync_test
    genius_node
)

if(WIN32)
    target_link_options(migration_sync_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node>)
elseif(APPLE)
    target_link_options(migration_sync_test PUBLIC -force_load "$<TARGET_FILE:genius_node>")
else()
    target_link_options(migration_sync_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node>"
        "-Wl,--no-whole-archive"
    )
endif()


addtest(transaction_crash_test
transaction_crash_test.cpp
    )

target_include_directories(transaction_crash_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(transaction_crash_test
    genius_node
)

if(WIN32)
    target_link_options(transaction_crash_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node>)
elseif(APPLE)
    target_link_options(transaction_crash_test PUBLIC -force_load "$<TARGET_FILE:genius_node>")
else()
    target_link_options(transaction_crash_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node>"
        "-Wl,--no-whole-archive"
    )
endif()

add_custom_target(node10_0_2_0
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/node10_0_2_0/"
        "$<TARGET_FILE_DIR:migration_sync_test>/node10_0_2_0"
        COMMENT "Copying data"
)
#add_dependencies(migration_sync_test node10_0_2_0)

add_custom_target(node20_0_2_0
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/node20_0_2_0/"
        "$<TARGET_FILE_DIR:migration_sync_test>/node20_0_2_0"
        COMMENT "Copying data"
)
#add_dependencies(migration_sync_test node20_0_2_0)
