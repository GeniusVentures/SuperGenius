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

# Several suites in this directory boot multiple full nodes; running them
# concurrently starves CPU on smaller CI runners (observed: a genesis creator
# starved for 30s while eight test binaries ran), stalling startup past any
# sane timeout. Sharing this lock serializes them against the other
# node-booting suites while light suites keep running in parallel.
set_tests_properties(archive_replication_test
    PROPERTIES RESOURCE_LOCK genius_node_pool)
