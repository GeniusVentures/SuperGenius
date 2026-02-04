
addtest(blockchain_genesis_test
    blockchain_genesis_test.cpp
    )

addtest(consensus_certificate_test
    consensus_certificate_test.cpp
)
target_link_libraries(consensus_certificate_test
    blockchain_genesis
    rapidjson
    base_crdt_test
)

target_include_directories(blockchain_genesis_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(blockchain_genesis_test
    genius_node
    rapidjson
)
if(WIN32)
    target_link_options(blockchain_genesis_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node>)

elseif(APPLE)
    target_link_options(blockchain_genesis_test PUBLIC -force_load "$<TARGET_FILE:genius_node>")

else()
    target_link_options(blockchain_genesis_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node>"
        "-Wl,--no-whole-archive"
    )
endif()
