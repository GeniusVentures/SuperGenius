addtest(transaction_error_test
transaction_error_test.cpp
    )

target_include_directories(transaction_error_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(transaction_error_test
    sgns_account
)

if(WIN32)
    target_link_options(transaction_error_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:sgns_account>)

elseif(APPLE)
    target_link_options(transaction_error_test PUBLIC -force_load "$<TARGET_FILE:sgns_account>")

else()
    target_link_options(transaction_error_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:sgns_account>"
        "-Wl,--no-whole-archive"
    )
endif()

addtest(delete_node_test
delete_node_test.cpp
    )

target_include_directories(delete_node_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(delete_node_test
    sgns_account
)

if(WIN32)
    target_link_options(delete_node_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:sgns_account>)

elseif(APPLE)
    target_link_options(delete_node_test PUBLIC -force_load "$<TARGET_FILE:sgns_account>")

else()
    target_link_options(delete_node_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:sgns_account>"
        "-Wl,--no-whole-archive"
    )
endif()
