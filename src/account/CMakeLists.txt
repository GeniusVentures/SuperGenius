
add_proto_library(SGTransactionProto proto/SGTransaction.proto)

add_library(sgns_account
    TransactionManager.cpp
    TransferTransaction.cpp
    MintTransaction.cpp
    ProcessingTransaction.cpp
    AccountManager.cpp
)

target_link_libraries(sgns_account
    blockchain_common
    block_header_repository
    block_storage
    logger
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
    ipfs-bitswap-cpp
	ipfs-pubsub
    SGTransactionProto
    Boost::boost
	logger
    ${WIN_CRYPT_LIBRARY}
)