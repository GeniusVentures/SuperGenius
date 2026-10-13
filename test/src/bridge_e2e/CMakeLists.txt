
addtest(bridge_e2e_test
    bridge_e2e_test.cpp
    )

target_include_directories(bridge_e2e_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(bridge_e2e_test
    genius_node_test
    json_secure_storage
    )
if(WIN32)
    target_link_options(bridge_e2e_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)

elseif(APPLE)
    target_link_options(bridge_e2e_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")

else()
    target_link_options(bridge_e2e_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

addtest(bridge_e2e_chainlist_test
    bridge_e2e_chainlist_test.cpp
    )

target_link_libraries(bridge_e2e_chainlist_test
    genius_node_test
    json_secure_storage
    )
if(WIN32)
    target_link_options(bridge_e2e_chainlist_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)

elseif(APPLE)
    target_link_options(bridge_e2e_chainlist_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")

else()
    target_link_options(bridge_e2e_chainlist_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

addtest(bridge_anvil_e2e_test
    bridge_anvil_e2e_test.cpp
    )

target_include_directories(bridge_anvil_e2e_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(bridge_anvil_e2e_test
    genius_node_test
    json_secure_storage
    )
if(WIN32)
    target_link_options(bridge_anvil_e2e_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)

elseif(APPLE)
    target_link_options(bridge_anvil_e2e_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")

else()
    target_link_options(bridge_anvil_e2e_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

addtest(bridge_anvil_catchup_e2e_test
    bridge_anvil_catchup_e2e_test.cpp
    )

target_include_directories(bridge_anvil_catchup_e2e_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(bridge_anvil_catchup_e2e_test
    genius_node_test
    json_secure_storage
    )
if(WIN32)
    target_link_options(bridge_anvil_catchup_e2e_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)

elseif(APPLE)
    target_link_options(bridge_anvil_catchup_e2e_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")

else()
    target_link_options(bridge_anvil_catchup_e2e_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

addtest(bridge_sepolia_e2e_test
    bridge_sepolia_e2e_test.cpp
    )

target_include_directories(bridge_sepolia_e2e_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(bridge_sepolia_e2e_test
    genius_node_test
    json_secure_storage
    )
if(WIN32)
    target_link_options(bridge_sepolia_e2e_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)

elseif(APPLE)
    target_link_options(bridge_sepolia_e2e_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")

else()
    target_link_options(bridge_sepolia_e2e_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

addtest(bridge_rlpx_e2e_test
    bridge_rlpx_e2e_test.cpp
    )

target_include_directories(bridge_rlpx_e2e_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})
target_include_directories(bridge_rlpx_e2e_test PRIVATE ${CMAKE_SOURCE_DIR}/evmrelay/examples)

target_link_libraries(bridge_rlpx_e2e_test
    genius_node_test
    json_secure_storage
    )
if(WIN32)
    target_link_options(bridge_rlpx_e2e_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)

elseif(APPLE)
    target_link_options(bridge_rlpx_e2e_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")

else()
    target_link_options(bridge_rlpx_e2e_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

# The requires_foundry label marks these tests as needing the Foundry binaries
# (anvil + cast) on PATH (see anvil_fixture.hpp / each fixture's runtime
# GTEST_SKIP). CI excludes them deterministically via
# `ctest -LE requires_foundry` (cmake.yml) instead of relying on that PATH probe,
# which only "passes" them on runners without Foundry. bridge_e2e_test and
# bridge_e2e_chainlist_test do NOT use the anvil fixture and stay unlabeled.
# Local `ctest` runs still execute them and skip inside the fixture when Foundry
# is missing.
set_tests_properties(
    bridge_anvil_e2e_test
    bridge_anvil_catchup_e2e_test
    bridge_sepolia_e2e_test
    bridge_rlpx_e2e_test
    PROPERTIES LABELS "requires_foundry"
)
