addtest(multi_account_test
    multi_account_sync.cpp
)
target_include_directories(multi_account_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(multi_account_test
    genius_node_test
    json_secure_storage
)

# 8 multi-node integration tests in one binary: ~300 s unloaded, ~700 s when
# ctest -j 4 piles it together with the other heavy suites on the same runner.
set_tests_properties(multi_account_test PROPERTIES TIMEOUT 1200)

if(WIN32)
    target_link_options(multi_account_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)

elseif(APPLE)
    target_link_options(multi_account_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")

else()
    target_link_options(multi_account_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

addtest(policy_lifetime_multi_account_test
    policy_lifetime_multi_account_test.cpp
)

target_include_directories(policy_lifetime_multi_account_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(policy_lifetime_multi_account_test
    genius_node_test
    json_secure_storage
    local_trust_admin
)

if(WIN32)
    target_link_options(policy_lifetime_multi_account_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(policy_lifetime_multi_account_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(policy_lifetime_multi_account_test PUBLIC
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
set_tests_properties(multi_account_test policy_lifetime_multi_account_test
    PROPERTIES RESOURCE_LOCK genius_node_pool)
