
addtest(bridge_e2e_test
    bridge_e2e_test.cpp
    )

target_include_directories(bridge_e2e_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(bridge_e2e_test
    genius_node_test
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
