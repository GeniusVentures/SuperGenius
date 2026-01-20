addtest(token_amount_test
    token_amount_test.cpp
)

target_include_directories(token_amount_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(token_amount_test
    genius_node
)

if(MSVC)
    target_link_options(token_amount_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node>)
elseif(APPLE)
    target_link_options(token_amount_test PUBLIC -force_load "$<TARGET_FILE:genius_node>")
else()
    target_link_options(token_amount_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node>"
        "-Wl,--no-whole-archive"
    )
endif()

addtest(account_balance_test
        utxo_manager_test.cpp
)

target_include_directories(account_balance_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(account_balance_test
    genius_node
    json_secure_storage
)

if(MSVC)
    target_link_options(account_balance_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node>)
elseif(APPLE)
    target_link_options(account_balance_test PUBLIC -force_load "$<TARGET_FILE:genius_node>")
else()
    target_link_options(account_balance_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node>"
        "-Wl,--no-whole-archive"
    )
endif()
