add_executable(echoClient echo_client.cpp)
target_link_libraries(echoClient 
    Boost::Boost.DI
    ipfs-pubsub
    crdt_graphsync_dagsyncer
    p2p::p2p_peer_address
    p2p::p2p_protocol_echo
    Boost::program_options
    ${WIN_CRYPT_LIBRARY}
    )