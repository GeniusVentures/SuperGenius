addtest(startup_wiring_test
    startup_wiring_test.cpp
)

target_link_libraries(startup_wiring_test
    genius_node_test
    json_secure_storage
)

# Copy bridge_chains_config.json to the build root so tests can find it
configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/../../../src/account/bridge_chains_config.json
    ${CMAKE_BINARY_DIR}/bridge_chains_config.json
    COPYONLY
)

if(MSVC)
    target_link_options(startup_wiring_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(startup_wiring_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(startup_wiring_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()
