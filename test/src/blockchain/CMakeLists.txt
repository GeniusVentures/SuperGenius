
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

#addtest(consensus_certificate_test
#    consensus_certificate_test.cpp
#)
#target_link_libraries(consensus_certificate_test
#    blockchain_genesis
#    rapidjson
#    base_crdt_test
#)

# ConsensusManager requires 6 constructor dependencies (ValidatorRegistry, GlobalDB,
# GossipPubSub, Signer, etc.) — cannot be instantiated in isolation.
# Test deferred to E2E integration (Phase 4).
# GetSlotKey dispatches via registered handler map — no ConsensusManager instance needed.
addtest(consensus_slot_key_test
    consensus_slot_key_test.cpp
)
target_link_libraries(consensus_slot_key_test
    blockchain_genesis
    genius_node_test
    rapidjson
)

set(CONSENSUS_PENDING_LIFECYCLE_SOURCE consensus_pending_lifecycle)
set(CONSENSUS_PENDING_LIFECYCLE_TARGET consensus_pending_lifecycle)
addtest(consensus_pending_lifecycle_test
    ${CONSENSUS_PENDING_LIFECYCLE_SOURCE}_test.cpp
)
target_link_libraries(${CONSENSUS_PENDING_LIFECYCLE_TARGET}_test
    blockchain_genesis
    genius_node_test
    rapidjson
    base_crdt_test
)

addtest(validator_registry_certificate_lookup_test
    validator_registry_certificate_lookup_test.cpp
)
target_link_libraries(validator_registry_certificate_lookup_test
    blockchain_genesis
    genius_node_test
    rapidjson
    base_crdt_test
)

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
