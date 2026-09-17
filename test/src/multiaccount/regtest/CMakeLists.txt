addtest(child_registration_test
    child_registration.cpp
)

target_include_directories(child_registration_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(child_registration_test
    genius_node_test
    base_crdt_test
    json_secure_storage
)

if(WIN32)
    target_link_options(child_registration_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)

elseif(APPLE)
    target_link_options(child_registration_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")

else()
    target_link_options(child_registration_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()
