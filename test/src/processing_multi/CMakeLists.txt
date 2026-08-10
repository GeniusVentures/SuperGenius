
addtest(processing_multi_test
    processing_multi_test.cpp
    )

target_include_directories(processing_multi_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(processing_multi_test
    genius_node_test
    json_secure_storage
)
if(WIN32)
    target_link_options(processing_multi_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)

elseif(APPLE)
    target_link_options(processing_multi_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")

else()
    target_link_options(processing_multi_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()
