
addtest(account_creation_test
    account_creation_test.cpp
    )

target_include_directories(account_creation_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(account_creation_test
    sgns_account
)
if(WIN32)
    target_link_options(account_creation_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:sgns_account>)

elseif(APPLE)
    target_link_options(account_creation_test PUBLIC -force_load "$<TARGET_FILE:sgns_account>")

else()
    target_link_options(account_creation_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:sgns_account>"
        "-Wl,--no-whole-archive"
    )
endif()