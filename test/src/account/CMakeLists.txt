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

addtest(utxo_manager_test
    utxo_manager_test.cpp
)

target_include_directories(utxo_manager_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(utxo_manager_test
    genius_node
    json_secure_storage
    base_crdt_test
)

if(MSVC)
    target_link_options(utxo_manager_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node>)
elseif(APPLE)
    target_link_options(utxo_manager_test PUBLIC -force_load "$<TARGET_FILE:genius_node>")
else()
    target_link_options(utxo_manager_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node>"
        "-Wl,--no-whole-archive"
    )
endif()
