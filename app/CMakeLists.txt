add_executable (sgns_demo
    runner.cpp
    SuperGeniusDemoApp.cpp
    integration/CComponentFactory.cpp
    )

target_link_libraries(sgns_demo
    logger
    Boost::system
    Boost::thread
    Boost::filesystem
    ipfs_blockservice
    validating_node_application
    app_state_manager
    app_config_impl
    local_key_storage
    configuration_storage
    block_header_repository
    clock
    rocksdb
    hasher
    vrf_provider
    block_storage
    production_lottery
    changes_tracker
    trie_serializer
    supergenius_trie_factory
    block_tree
    extrinsic_observer
    author_api_service
    transaction_pool
    gossiper_broadcast
    waitable_timer
    production
    remote_sync_protocol_client
    dummy_sync_protocol_client
    production_synchronizer
    block_validator
    sr25519_provider
    epoch_storage
    authority_manager
    proposer
    finality
    environment
    ed25519_provider
    sgns_router
    sync_protocol_observer
    api_service
    api_transport
    api_jrpc_server
    chain_api_service
    state_api_service
    system_api_service
    author_api_service
    api_author_requests
    p2p::p2p_basic_host
    p2p::p2p_default_network
    p2p::p2p_peer_repository
    p2p::p2p_inmem_address_repository
    p2p::p2p_inmem_key_repository
    p2p::p2p_inmem_protocol_repository
    p2p::p2p_literals
    p2p::p2p_kademlia
    p2p::p2p_identify
    p2p::p2p_ping
    p2p::p2p_peer_address
    p2p::p2p_protocol_echo
)

if(FORCE_MULTIPLE)
    set_target_properties(sgns_demo PROPERTIES LINK_FLAGS ${MULTIPLE_OPTION})
  endif()

  supergenius_install(sgns_demo)
