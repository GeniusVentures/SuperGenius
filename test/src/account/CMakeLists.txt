addtest(token_amount_test
token_amount_test.cpp
)

target_include_directories(token_amount_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(token_amount_test
    sgns_account
)

if(MSVC)
    target_link_options(token_amount_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:sgns_account>)
elseif(APPLE)
    target_link_options(token_amount_test PUBLIC -force_load "$<TARGET_FILE:sgns_account>")
else()
    target_link_options(token_amount_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:sgns_account>"
        "-Wl,--no-whole-archive"
    )
endif()

addtest(account_balance_test
account_balance_test.cpp
)

target_include_directories(account_balance_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(account_balance_test
    sgns_account
)

if(MSVC)
    target_link_options(account_balance_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:sgns_account>)
elseif(APPLE)
    target_link_options(account_balance_test PUBLIC -force_load "$<TARGET_FILE:sgns_account>")
else()
    target_link_options(account_balance_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:sgns_account>"
        "-Wl,--no-whole-archive"
    )
endif()