
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

# Phase 6 (D-01): proto-only slot-hash field tests. Does not require a fully
# wired ConsensusManager — only the regenerated Consensus.pb.h and
# VoteSigningBytes from ConsensusAuth.hpp.
addtest(consensus_vote_slot_test
    consensus_vote_slot_test.cpp
)
target_link_libraries(consensus_vote_slot_test
    blockchain_genesis
)

# Phase 6 (D-06): slot-quorum arithmetic unit tests. Exercises the pure
# static EvaluateSlotQuorumStatic helper directly -- no GlobalDB wiring needed.
addtest(validator_registry_slot_quorum_test
    validator_registry_slot_quorum_test.cpp
)
target_link_libraries(validator_registry_slot_quorum_test
    blockchain_genesis
)

# Phase 6 (D-06): ConsensusManager::IsBridgeMintSubject discriminator tests.
# Exercises the static discriminator directly -- no ConsensusManager instance.
addtest(consensus_bridge_mint_subject_test
    consensus_bridge_mint_subject_test.cpp
)
target_link_libraries(consensus_bridge_mint_subject_test
    blockchain_genesis
)

# Phase 6 (D-08): REGULAR -> FULL promotion decision unit tests. Exercises the
# pure static EvaluateRegularPromotionStatic helper directly -- no GlobalDB
# wiring needed (mirrors the EvaluateSlotQuorumStatic pattern from 06-02).
addtest(validator_registry_promotion_test
    validator_registry_promotion_test.cpp
)
target_link_libraries(validator_registry_promotion_test
    blockchain_genesis
)

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

addtest(consensus_certificate_store_test
    consensus_certificate_store_test.cpp
)
target_link_libraries(consensus_certificate_store_test
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

# Node startup-timing tests: genesis-node READY latency and regular-node READY
# latency after the genesis node is ready. Same wiring as blockchain_genesis_test
# (whole-archive genius_node_test + rapidjson + AsyncIOManager include).
addtest(node_startup_test
    node_startup_test.cpp
    )
target_include_directories(node_startup_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(node_startup_test
    genius_node_test
    rapidjson
    )
if(WIN32)
    target_link_options(node_startup_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)

elseif(APPLE)
    target_link_options(node_startup_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")

else()
    target_link_options(node_startup_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()
