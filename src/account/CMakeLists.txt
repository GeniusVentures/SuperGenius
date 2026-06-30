add_proto_library(SGTransactionProto proto/SGTransaction.proto)
add_proto_library(SGAccountCommProto proto/SGAccountComm.proto)

if(BUILD_WITH_PROOFS)
    add_compile_definitions(_PROOF_ENABLED)
endif()

add_library(sgns_genius_account
    UTXOStructs.cpp
    UTXOManager.cpp
    MigrationAllowList.cpp
    GeniusAccount.cpp
    AccountMessenger.cpp
)

target_link_libraries(sgns_genius_account
    PUBLIC
    Boost::headers
    ProofSystem
    outcome
    logger
    crdt_globaldb
    ipfs-pubsub
    TrustWalletCore
    PRIVATE
    hasher
    json_secure_storage
    secure_storage
    component_factory
    SGAccountCommProto
    SGTransactionProto
    ${WIN_API_LIBRARY}
    ${USER_ENV_LIBRARY}
    wallet_core_rs
    TrezorCrypto
    protobuf::libprotobuf
    ${WIN_CRYPT_LIBRARY}
)

target_precompile_headers(sgns_genius_account PUBLIC
    $<BUILD_INTERFACE:<ProofSystem/EthereumKeyGenerator.hpp$<ANGLE-R>>
)

target_include_directories(
    sgns_genius_account
    PRIVATE
    ${AsyncIOManager_INCLUDE_DIR}
)

if(MSVC)
    target_compile_options(sgns_genius_account PUBLIC /constexpr:steps1500000)
elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
    target_compile_options(sgns_genius_account PUBLIC -fconstexpr-steps=10000000)
endif()
add_library(genius_node
    IGeniusTransactions.cpp
    TransferTransaction.cpp
    MintTransaction.cpp
    MintTransactionV2.cpp
    MigrationTransaction.cpp
    ProcessingTransaction.cpp
    EscrowTransaction.cpp
    InputValidators.cpp
    TransactionManager.cpp
    TokenAmount.cpp
    BridgeConsensusAdapter.cpp
    MigrationManager.cpp
    Migration0_2_0To1_0_0.cpp
    Migration1_0_0To3_4_0.cpp
    Migration3_4_0To3_5_0.cpp
    Migration3_5_0To3_6_0.cpp
    Migration3_6_0To3_7_0.cpp
    GeniusNode.cpp
)

target_link_libraries(genius_node
    PUBLIC
    Boost::headers
    ProofSystem
    outcome
    logger
    crdt_globaldb
    processing_service
    gnus_upnp
    sgns_genius_account
    blockchain_genesis
 ProcessingBase
    evmrelay
    PRIVATE
    component_factory
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
    ScaledInteger
    sgns_version
    AsyncIOManager
    ${WIN_API_LIBRARY}
    ${USER_ENV_LIBRARY}
    protobuf::libprotobuf
    rapidjson
    coinprices
    ${WIN_CRYPT_LIBRARY}
    transfer_proof
    processing_proof
)

add_dependencies(genius_node rlp)

#set_target_properties(genius_node PROPERTIES PU@)
if(MSVC)
    target_compile_options(genius_node PUBLIC /constexpr:steps1500000)
elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
    target_compile_options(genius_node PUBLIC -fconstexpr-steps=10000000)
endif()

# Install both libraries
supergenius_install(sgns_genius_account)
supergenius_install(genius_node)
