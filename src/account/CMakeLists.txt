
add_proto_library(SGTransactionProto proto/SGTransaction.proto)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../ProofSystem "${CMAKE_BINARY_DIR}/ProofSystem")

add_library(sgns_account
    TransactionManager.cpp
    TransferTransaction.cpp
    MintTransaction.cpp
    ProcessingTransaction.cpp
    EscrowTransaction.cpp
    GeniusAccount.cpp
    GeniusNode.cpp
    UTXOTxParameters.cpp
)

target_link_libraries(sgns_account
    GeniusKDF
    json_secure_storage
    component_factory
    processing_service
    block_header_repository
    block_storage
    logger
    crdt_globaldb
    crdt_graphsync_dagsyncer
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
    SGTransactionProto
    Boost::boost
    logger
    gnus_upnp
    KeyGeneration
    ${WIN_CRYPT_LIBRARY}
)
set_target_properties(sgns_account PROPERTIES PUBLIC_HEADER "GeniusNode.hpp")

supergenius_install(sgns_account)