addtest(multi_account_test
    multi_account_sync.cpp
)
target_include_directories(multi_account_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(multi_account_test
    genius_node_test
    json_secure_storage
)

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
