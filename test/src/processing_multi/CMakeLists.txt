
addtest(processing_multi_test
    processing_multi_test.cpp
    )

target_include_directories(processing_multi_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(processing_multi_test
    sgns_account
)
if(WIN32)
    target_link_options(processing_multi_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:sgns_account>)

elseif(APPLE)
    target_link_options(processing_multi_test PUBLIC -force_load "$<TARGET_FILE:sgns_account>")

else()
    target_link_options(processing_multi_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:sgns_account>"
        "-Wl,--no-whole-archive"
    )
endif()