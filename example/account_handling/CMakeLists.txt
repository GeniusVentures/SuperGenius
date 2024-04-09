add_executable(account_handling
    AccountHandling.cpp
    )
    include_directories(
        ${PROJECT_SOURCE_DIR}/src
        )
    target_include_directories(account_handling PRIVATE ${GSL_INCLUDE_DIR}) 
    target_link_libraries(account_handling PRIVATE
#    ipfs-lite-cpp::ipfs_datastore_rocksdb
#    ipfs-lite-cpp::buffer
#    ipfs-lite-cpp::ipld_node
#    ipfs-lite-cpp::ipfs_merkledag_service
#    ipfs-lite-cpp::graphsync
    blockchain_common
    block_header_repository
    block_storage
    logger
	#processing_service
	crdt_globaldb
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
    Boost::Boost.DI
    Boost::program_options
    ipfs-bitswap-cpp
	ipfs-pubsub
    ${WIN_CRYPT_LIBRARY}
    )