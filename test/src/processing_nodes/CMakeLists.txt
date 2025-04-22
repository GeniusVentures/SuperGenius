
addtest(processing_nodes_test
    processing_nodes_test.cpp
    )

target_include_directories(processing_nodes_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(processing_nodes_test
    sgns_account
)
if(WIN32)
    target_link_options(processing_nodes_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:sgns_account>)

elseif(APPLE)
    target_link_options(processing_nodes_test PUBLIC -force_load "$<TARGET_FILE:sgns_account>")

else()
    target_link_options(processing_nodes_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:sgns_account>"
        "-Wl,--no-whole-archive"
    )
endif()

addtest(globaldb_integration_gtest
    globaldb_integration_gtest.cpp
    )

target_include_directories(globaldb_integration_gtest PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(globaldb_integration_gtest
    sgns_account
)
if(WIN32)
    target_link_options(globaldb_integration_gtest PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:sgns_account>)

elseif(APPLE)
    target_link_options(globaldb_integration_gtest PUBLIC -force_load "$<TARGET_FILE:sgns_account>")

else()
    target_link_options(globaldb_integration_gtest PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:sgns_account>"
        "-Wl,--no-whole-archive"
    )
endif()