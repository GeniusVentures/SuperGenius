addtest(startup_wiring_test
    startup_wiring_test.cpp
)

target_link_libraries(startup_wiring_test
    genius_node_test
    json_secure_storage
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
