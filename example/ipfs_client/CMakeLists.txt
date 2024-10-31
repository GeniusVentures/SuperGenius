add_executable(ipfs_client
    ipfs.cpp
    ping_session.cpp
    ping_session.hpp
    ipfs_dht.cpp
    ipfs_dht.hpp
    )


# Copy the required headers to install directory
include_directories(
    ${PROJECT_SOURCE_DIR}/src
    )

target_include_directories(ipfs_client PRIVATE ${GSL_INCLUDE_DIR})


target_link_libraries(ipfs_client
    ipfs-lite-cpp::ipld_node_protobuf
    ipfs-lite-cpp::cbor
    logger
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
    ${WIN_CRYPT_LIBRARY}
)
