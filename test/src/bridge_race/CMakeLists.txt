addtest(bridge_race_single_burn_test
    bridge_race_single_burn_test.cpp
    )

target_include_directories(bridge_race_single_burn_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})
target_include_directories(bridge_race_single_burn_test PRIVATE ${CMAKE_SOURCE_DIR}/test/src/bridge_e2e)

target_link_libraries(bridge_race_single_burn_test
    genius_node_test
    json_secure_storage
    )
if(WIN32)
    target_link_options(bridge_race_single_burn_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)

elseif(APPLE)
    target_link_options(bridge_race_single_burn_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")

else()
    target_link_options(bridge_race_single_burn_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

# D-15/Pitfall 2: 11-node startup cost is higher than the fast bridge_e2e suite's
# default timeout — own extended TIMEOUT, isolated ctest target.
set_tests_properties(bridge_race_single_burn_test PROPERTIES TIMEOUT 500)

addtest(bridge_race_batch_test
    bridge_race_batch_test.cpp
    )

target_include_directories(bridge_race_batch_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})
target_include_directories(bridge_race_batch_test PRIVATE ${CMAKE_SOURCE_DIR}/test/src/bridge_e2e)

target_link_libraries(bridge_race_batch_test
    genius_node_test
    json_secure_storage
    )
if(WIN32)
    target_link_options(bridge_race_batch_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)

elseif(APPLE)
    target_link_options(bridge_race_batch_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")

else()
    target_link_options(bridge_race_batch_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

set_tests_properties(bridge_race_batch_test PROPERTIES TIMEOUT 180)

addtest(bridge_race_fault_rpc_test
    bridge_race_fault_rpc_test.cpp
    )

target_include_directories(bridge_race_fault_rpc_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})
target_include_directories(bridge_race_fault_rpc_test PRIVATE ${CMAKE_SOURCE_DIR}/test/src/bridge_e2e)

target_link_libraries(bridge_race_fault_rpc_test
    genius_node_test
    json_secure_storage
    mock_rpc_transport
    )
if(WIN32)
    target_link_options(bridge_race_fault_rpc_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)

elseif(APPLE)
    target_link_options(bridge_race_fault_rpc_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")

else()
    target_link_options(bridge_race_fault_rpc_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

set_tests_properties(bridge_race_fault_rpc_test PROPERTIES TIMEOUT 180)

addtest(bridge_race_fault_kill_test
    bridge_race_fault_kill_test.cpp
    )

target_include_directories(bridge_race_fault_kill_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})
target_include_directories(bridge_race_fault_kill_test PRIVATE ${CMAKE_SOURCE_DIR}/test/src/bridge_e2e)

target_link_libraries(bridge_race_fault_kill_test
    genius_node_test
    json_secure_storage
    )
if(WIN32)
    target_link_options(bridge_race_fault_kill_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)

elseif(APPLE)
    target_link_options(bridge_race_fault_kill_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")

else()
    target_link_options(bridge_race_fault_kill_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

set_tests_properties(bridge_race_fault_kill_test PROPERTIES TIMEOUT 180)

addtest(bridge_race_fault_partition_test
    bridge_race_fault_partition_test.cpp
    )

target_include_directories(bridge_race_fault_partition_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})
target_include_directories(bridge_race_fault_partition_test PRIVATE ${CMAKE_SOURCE_DIR}/test/src/bridge_e2e)

target_link_libraries(bridge_race_fault_partition_test
    genius_node_test
    json_secure_storage
    )
if(WIN32)
    target_link_options(bridge_race_fault_partition_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)

elseif(APPLE)
    target_link_options(bridge_race_fault_partition_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")

else()
    target_link_options(bridge_race_fault_partition_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

set_tests_properties(bridge_race_fault_partition_test PROPERTIES TIMEOUT 180)
