add_proto_library(SGTransactionProto proto/SGTransaction.proto)
add_proto_library(SGAccountCommProto proto/SGAccountComm.proto)

if (BUILD_WITH_PROOFS)
    add_compile_definitions(_PROOF_ENABLED)
endif()

# Create the GeniusAccount library
add_library(sgns_genius_account
    UTXOManager.cpp
    GeniusAccount.cpp
    AccountMessenger.cpp
)

set_target_properties(sgns_genius_account PROPERTIES UNITY_BUILD ON)

target_link_libraries(sgns_genius_account
    PUBLIC
    Boost::headers
    KeyGeneration
    outcome
    logger
    crdt_globaldb
    ipfs-pubsub
    PRIVATE
    secure_storage
    component_factory
    SGAccountCommProto
    ${WIN_API_LIBRARY}
    ${USER_ENV_LIBRARY}
    TrustWalletCore
    wallet_core_rs
    TrezorCrypto
    protobuf::libprotobuf
    ${WIN_CRYPT_LIBRARY}
)

target_include_directories(
    sgns_genius_account
    PRIVATE
    ${AsyncIOManager_INCLUDE_DIR}
)

if (MSVC)
    target_compile_options(sgns_genius_account PUBLIC /constexpr:steps1500000)
elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
    target_compile_options(sgns_genius_account PUBLIC -fconstexpr-steps=10000000)
endif()

# Create the main account library (without GeniusAccount files)
add_library(genius_node
    IGeniusTransactions.cpp
    TransferTransaction.cpp
    MintTransaction.cpp
    ProcessingTransaction.cpp
    EscrowTransaction.cpp
    EscrowReleaseTransaction.cpp
    TransactionManager.cpp
    TokenAmount.cpp
    MigrationManager.cpp
    Migration0_2_0To1_0_0.cpp
    Migration1_0_0To3_4_0.cpp
    Migration3_4_0To3_5_0.cpp
    Migration3_5_0To3_5_1.cpp
    Migration3_5_1To3_6_0.cpp
    GeniusNode.cpp
)

set_target_properties(genius_node PROPERTIES UNITY_BUILD ON)

target_link_libraries(genius_node
    PUBLIC
    Boost::headers
    KeyGeneration
    outcome
    logger
    crdt_globaldb
    processing_service
    gnus_upnp
    sgns_genius_account  # Link to the GeniusAccount library
    blockchain_genesis
	ProcessingBase
    PRIVATE
    json_secure_storage
    secure_storage
    component_factory
    block_header_repository
    block_storage
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
    TrustWalletCore
    wallet_core_rs
    TrezorCrypto
    protobuf::libprotobuf
    rapidjson
    coinprices
    ${WIN_CRYPT_LIBRARY}
    transfer_proof
    processing_proof
)

set_target_properties(genius_node PROPERTIES PUBLIC_HEADER "GeniusNode.hpp")
if (MSVC)
    target_compile_options(genius_node PUBLIC /constexpr:steps1500000)
elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
    target_compile_options(genius_node PUBLIC -fconstexpr-steps=10000000)
endif()

# Install both libraries
supergenius_install(sgns_genius_account)
supergenius_install(genius_node)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/../../ProofSystem/include/" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h*")
