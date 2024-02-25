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
      #protobuf::libprotobuf
      #${USER_ENV_LIBRARY}  # On windows platform , sr25519 library needs extra library userenv, so folllowing code is added
      #${WIN_CRYPT_LIBRARY}
      )
  if(FORCE_MULTILE)
    set_target_properties(sgns_demo PROPERTIES LINK_FLAGS ${MULTIPLE_OPTION})
  endif()

  supergenius_install(sgns_demo)
