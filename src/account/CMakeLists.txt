
add_proto_library(SGTransactionProto proto/SGTransaction.proto)

set(BUILD_WITH_PROOFS OFF CACHE BOOL "Whether proofs are being generated/verified or not")

if (BUILD_WITH_PROOFS)
    add_compile_definitions(_PROOF_ENABLED)
endif()


add_library(sgns_account
    TransactionManager.cpp
    TransferTransaction.cpp
    MintTransaction.cpp
    ProcessingTransaction.cpp
    EscrowTransaction.cpp
    GeniusAccount.cpp
    GeniusNode.cpp
    UTXOTxParameters.cpp
    IGeniusTransactions.cpp
)

target_link_libraries(sgns_account
    PUBLIC
    Boost::headers
    KeyGeneration
    outcome
    logger
    crdt_globaldb
    PRIVATE
    json_secure_storage
    component_factory
    processing_service
    block_header_repository
    block_storage
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
    logger
    gnus_upnp
    AsyncIOManager
    ${WIN_API_LIBRARY}
    ${USER_ENV_LIBRARY}
    ${TrustWalletCore}
    ${wallet_core_rs}
    ${TrezorCrypto}
    protobuf::libprotobuf
    rapidjson
    ${WIN_CRYPT_LIBRARY}
)

if (BUILD_WITH_PROOFS)
    target_link_libraries(sgns_account
    PRIVATE
    transfer_proof
    processing_proof
    )
endif()

target_include_directories(
    sgns_account
    PRIVATE
    ${AsyncIOManager_INCLUDE_DIR}
    ${TrustWalletCore_INCLUDE_DIR}
)

set_target_properties(sgns_account PROPERTIES PUBLIC_HEADER "GeniusNode.hpp")
if (MSVC)
    target_compile_options(sgns_account PRIVATE /constexpr:steps10000000)
endif()
if(ANDROID_ABI STREQUAL "armeabi-v7a")
    target_compile_options(sgns_account PRIVATE -fconstexpr-steps=1000000000 -fconstexpr-depth=1000000000)
endif()

supergenius_install(sgns_account)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/../../ProofSystem/include/" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h*")