add_proto_library(SGTransactionProto proto/SGTransaction.proto)
add_proto_library(SGAccountCommProto proto/SGAccountComm.proto)

if(BUILD_WITH_PROOFS)
    add_compile_definitions(_PROOF_ENABLED)
endif()

add_library(sgns_genius_account
    UTXOStructs.cpp
    UTXOManager.cpp
    MigrationAllowList.cpp
    GeniusSigner.cpp
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
    protobuf::libprotobuf
    libsecp256k1::secp256k1
    ${WIN_CRYPT_LIBRARY}
    TrezorCrypto
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

# BurnConfig: genesis-seeded, quorum-signed BURN_BASIS_POINTS value (BURN-01,
# BURN-02, BURN-03), built on top of SecureCrdt/TrustedPeerRegistry/GeniusAccount.
add_library(burnconfig
    BurnConfig.cpp
)
target_link_libraries(burnconfig
    PUBLIC
    trustedpeer
    securecrdt
    sgns_genius_account
)
supergenius_install(burnconfig)

set(GENIUS_NODE_SOURCES
    GeniusTransaction.cpp
    TransferTransaction.cpp
    MintTransaction.cpp
    MintTransactionV2.cpp
    MigrationTransaction.cpp
    EscrowTransaction.cpp
    GeniusInputValidator.cpp
    MigrationInputValidator.cpp
    InputValidators.cpp
    PublicChainInputValidator.cpp
    TransactionManager.cpp
    TokenAmount.cpp
    GeniusNode.cpp
    ChainRpcEndpointProvider.cpp
    BridgeRelayer.cpp
)

set(GENIUS_NODE_LIBS
    PRIVATE
    AsyncIOManager
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
    ipfs-bitswap-proto
    ipfs-unixfs-proto
    ipfs-bitswap-cpp
    AsyncIOManager
    Boost::Boost.DI
    ScaledInteger
    sgns_version
    ${WIN_API_LIBRARY}
    ${USER_ENV_LIBRARY}
    protobuf::libprotobuf
    rapidjson
    coinprices
    ${WIN_CRYPT_LIBRARY}
    transfer_proof
    processing_proof
    PUBLIC
    Boost::headers
    ProofSystem
    outcome
    logger
    crdt_globaldb
    SGTransactionProto
    processing_service
    gnus_upnp
    blockchain_genesis
    ProcessingBase
    evmrelay
    migration
    evm_watcher_service
    sgns_genius_account
    trustedpeer
    burnconfig
)


# Production library — uses OS keychain (Apple/Windows/Linux secure storage)
add_library(genius_node ${GENIUS_NODE_SOURCES})
target_link_libraries(genius_node ${GENIUS_NODE_LIBS})
target_include_directories(
    genius_node
    PRIVATE
    ${AsyncIOManager_INCLUDE_DIR}
)
# Test library — uses in-memory secure storage via DI factory (no keychain prompts)
add_library(genius_node_test ${GENIUS_NODE_SOURCES})
target_link_libraries(genius_node_test ${GENIUS_NODE_LIBS})

add_dependencies(genius_node rlp)

#set_target_properties(genius_node PROPERTIES PU@)
if(MSVC)
    target_compile_options(genius_node PUBLIC /constexpr:steps1500000)
elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
    target_compile_options(genius_node PUBLIC -fconstexpr-steps=10000000)
endif()

# Install production library only
supergenius_install(sgns_genius_account)
supergenius_install(genius_node)

# Copy default bridge chains config to test binary output for dev/test runs
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/bridge_chains_config.json
    ${CMAKE_BINARY_DIR}/test_bin/bridge_chains_config.json
    COPYONLY
)

# Install the default config alongside the binary for production
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/bridge_chains_config.json
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ── find_contract_creation_blocks — optional one-shot utility ────────────
# Downloads the GeniusDiamond deployment manifest, cross-references diamond
# addresses, and binary-searches eth_getCode for creation blocks using
# built-in public RPC endpoints (no API key needed).
# NOT part of the default build — invoke explicitly when deployments change:
#
#   cmake --build build --target find_contract_creation_blocks
#
# To override a chain's RPC endpoint, run the script directly:
#   python3 scripts/find_contract_creation_blocks.py \
#       src/account/bridge_chains_config.json \
#       --rpc ethereum-sepolia=https://my-node.com
add_custom_target(find_contract_creation_blocks
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/find_contract_creation_blocks.py
        ${CMAKE_SOURCE_DIR}/src/account/bridge_chains_config.json
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Cross-referencing GeniusDiamond manifest + discovering creation blocks"
    VERBATIM
)
