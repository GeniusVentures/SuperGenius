addtest(startup_wiring_test
    startup_wiring_test.cpp
)

addtest(trust_first_boot_e2e_test
    trust_first_boot_e2e_test.cpp
)

addtest(trust_restart_test
    trust_restart_test.cpp
)

addtest(trust_tamper_e2e_test
    trust_tamper_e2e_test.cpp
)

target_link_libraries(startup_wiring_test
    genius_node_test
    json_secure_storage
)

target_link_libraries(trust_first_boot_e2e_test
    genius_node_test
    local_trust_admin
    json_secure_storage
    rocksdb
)
target_include_directories(trust_first_boot_e2e_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

foreach(trust_test trust_restart_test trust_tamper_e2e_test)
    target_link_libraries(${trust_test}
        genius_node_test
        json_secure_storage
        rocksdb
    )
    target_include_directories(${trust_test} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
endforeach()

# Copy bridge_chains_config.json next to the test binary so direct execution
# and CTest use the same relative lookup path.
configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/../../../src/account/bridge_chains_config.json
    ${CMAKE_BINARY_DIR}/test_bin/bridge_chains_config.json
    COPYONLY
)

target_compile_definitions(startup_wiring_test
    PRIVATE
    STARTUP_WIRING_TEST_CONFIG_PATH="${CMAKE_BINARY_DIR}/test_bin/bridge_chains_config.json"
)

# CTest defaults CWD to this source/build subdirectory. Run from the binary
# directory because the test opens bridge_chains_config.json by relative path.
set_tests_properties(
    startup_wiring_test
    trust_first_boot_e2e_test
    trust_restart_test
    trust_tamper_e2e_test
    PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test_bin
)

if(MSVC)
    target_link_options(startup_wiring_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
    target_link_options(trust_first_boot_e2e_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
    target_link_options(trust_restart_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
    target_link_options(trust_tamper_e2e_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(startup_wiring_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
    target_link_options(trust_first_boot_e2e_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
    target_link_options(trust_restart_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
    target_link_options(trust_tamper_e2e_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(startup_wiring_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
    target_link_options(trust_first_boot_e2e_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
    foreach(trust_test trust_restart_test trust_tamper_e2e_test)
        target_link_options(${trust_test} PUBLIC
            "-Wl,--whole-archive"
            "$<TARGET_FILE:genius_node_test>"
            "-Wl,--no-whole-archive"
        )
    endforeach()
endif()
