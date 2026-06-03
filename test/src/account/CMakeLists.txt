addtest(token_id_test
    token_id_test.cpp
)

addtest(token_amount_test
    token_amount_test.cpp
)

target_link_libraries(token_amount_test
    genius_node_test
)

if(MSVC)
    target_link_options(token_amount_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(token_amount_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(token_amount_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

addtest(utxo_manager_test
    utxo_manager_test.cpp
)

target_link_libraries(utxo_manager_test
    genius_node_test
    json_secure_storage
    base_crdt_test
)

if(MSVC)
    target_link_options(utxo_manager_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(utxo_manager_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(utxo_manager_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()

addtest(account_management_test
    account_management_test.cpp
)

target_link_libraries(account_management_test genius_node_test json_secure_storage)

if(MSVC)
    target_link_options(account_management_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(account_management_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(account_management_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()
