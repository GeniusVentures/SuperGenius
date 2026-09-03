addtest(archive_replication_test
    archive_replication_test.cpp
    )

target_include_directories(archive_replication_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(archive_replication_test
    genius_node_test
    json_secure_storage
)

if(WIN32)
    target_link_options(archive_replication_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(archive_replication_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(archive_replication_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()
