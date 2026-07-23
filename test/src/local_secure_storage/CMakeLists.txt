addtest(secure_storage_test
    secure_storage_test.cpp
)
target_link_libraries(secure_storage_test
    secure_storage
)

addtest(json_migration_test
    json_migration_test.cpp
)
target_link_libraries(json_migration_test
    secure_storage
    json_secure_storage
    sgns_genius_account
)
add_custom_command(TARGET json_migration_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/json_storage/"
    "$<TARGET_FILE_DIR:json_migration_test>/j"
    COMMENT "Copying json_storage data"
)
