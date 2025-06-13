addtest(migration_sync_test
    migration_sync_test.cpp
)

target_include_directories(migration_sync_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(migration_sync_test sgns_account)

if(WIN32)
    target_link_options(migration_sync_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:sgns_account>)
elseif(APPLE)
    target_link_options(migration_sync_test PUBLIC -force_load "$<TARGET_FILE:sgns_account>")
else()
    target_link_options(migration_sync_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:sgns_account>"
        "-Wl,--no-whole-archive"
    )
endif()

# Add the custom copy steps as POST_BUILD to avoid circular dependency
add_custom_command(TARGET migration_sync_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/node10_0_2_0/"
            "$<TARGET_FILE_DIR:migration_sync_test>/node10_0_2_0"
    COMMENT "Copying node10_0_2_0 to build directory"
)

add_custom_command(TARGET migration_sync_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/node20_0_2_0/"
            "$<TARGET_FILE_DIR:migration_sync_test>/node20_0_2_0"
    COMMENT "Copying node20_0_2_0 to build directory"
)
