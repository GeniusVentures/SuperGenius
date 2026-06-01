
addtest(blockchain_genesis_test
    blockchain_genesis_test.cpp
    )

addtest(consensus_subject_test
    consensus_subject_test.cpp
)
target_link_libraries(consensus_subject_test
    blockchain_genesis
    rapidjson
)

addtest(bridge_consensus_adapter_test
    bridge_consensus_adapter_test.cpp
)
target_link_libraries(bridge_consensus_adapter_test
    genius_node
    rapidjson
)

#addtest(consensus_certificate_test
#    consensus_certificate_test.cpp
#)
#target_link_libraries(consensus_certificate_test
#    blockchain_genesis
#    rapidjson
#    base_crdt_test
#)

target_include_directories(blockchain_genesis_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(blockchain_genesis_test
    genius_node_test
    rapidjson
)
if(WIN32)
    target_link_options(blockchain_genesis_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)

elseif(APPLE)
    target_link_options(blockchain_genesis_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")

else()
    target_link_options(blockchain_genesis_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()
