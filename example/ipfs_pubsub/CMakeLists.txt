add_executable(ipfs_pubsub
    ipfs_pubsub.cpp
    )


# Copy the required headers to install directory
include_directories(
    ${PROJECT_SOURCE_DIR}/src
    )

target_include_directories(ipfs_pubsub PRIVATE ${GSL_INCLUDE_DIR})


target_link_libraries(ipfs_pubsub
#    ipfs-lite-cpp::ipfs_datastore_rocksdb
#    ipfs-lite-cpp::buffer
#    ipfs-lite-cpp::ipld_node
#    ipfs-lite-cpp::ipfs_merkledag_service
#    ipfs-lite-cpp::graphsync
    logger
	crdt_globaldb
	processing_service
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
	p2p::p2p_logger
    Boost::Boost.DI
    Boost::program_options
    ipfs-bitswap-cpp
	ipfs-pubsub
    ${WIN_CRYPT_LIBRARY}
    )
