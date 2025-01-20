
addtest(multi_account_test
    multi_account_sync.cpp
    )

target_include_directories(multi_account_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(multi_account_test
    sgns_account
)
if(WIN32)
    target_link_options(multi_account_test PUBLIC /WHOLEARCHIVE:"$<TARGET_FILE:sgns_account>")

elseif(APPLE)
    target_link_options(multi_account_test PUBLIC -force_load "$<TARGET_FILE:sgns_account>")

else()
    target_link_options(multi_account_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:sgns_account>"
        "-Wl,--no-whole-archive"
    )
endif()