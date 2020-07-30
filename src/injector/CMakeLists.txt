
add_library(application_injector
    application_injector.cpp
    )
target_link_libraries(application_injector
    app_state_manager
    p2p::p2p
    tsl::tsl_hat_trie
    Boost::Boost.DI
    rpc_thread_pool
    api_transport
    api_jrpc_server
    state_api_service
    trie_storage
    supergenius_trie
    supergenius_trie_factory
    trie_serializer
    supergenius_codec
    changes_tracker
    chain_api_service
    production
    production_lottery
    block_header_repository
    block_executor
    block_storage
    production_synchronizer
    block_tree
    block_validator
    buffer
    clock
    sr25519_provider
    configuration_storage
    binaryen_core_api
    binaryen_production_api
    binaryen_finality_api
    binaryen_block_builder_api
    trie_storage_provider
    author_api_service
    extrinsic_observer
    api_author_requests
    transaction_pool
    extension_factory
    epoch_storage
    gossiper_broadcast
    sgns_router
    leveldb
    local_key_storage
    outcome
    launcher
    environment
    proposer
    storage_wasm_provider
    remote_sync_protocol_client
    dummy_sync_protocol_client
    sync_protocol_observer
    binaryen_tagged_transaction_queue_api
    vrf_provider
    waitable_timer
    binaryen_wasm_executor
    )

add_library(syncing_node_injector
    syncing_node_injector.cpp
    )
target_link_libraries(syncing_node_injector
    application_injector
    syncing_round_observer
    syncing_production_observer
    )

add_library(validating_node_injector
    validating_node_injector.cpp
    )
target_link_libraries(validating_node_injector
    application_injector
    finality_dummy
    )

# add_library(block_producing_node_injector
#     block_producing_node_injector.cpp
#     )
# target_link_libraries(block_producing_node_injector
#     application_injector
#     syncing_round_observer
#     finality_dummy
#     )
