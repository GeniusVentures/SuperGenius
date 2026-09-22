# Shared by src/account (genius_node, genius_node_objs) and src/transaction
# (genius_transaction_objs). Lives here because it is common to both; the
# targets it names are defined in src/account and resolve at generate time.
set(GENIUS_NODE_LIBS
    PRIVATE
    component_factory
    crdt_graphsync_dagsyncer
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

add_subdirectory(api/transport)
add_subdirectory(account)
add_subdirectory(transaction)
add_subdirectory(base)
add_subdirectory(blockchain)
add_subdirectory(outcome)
add_subdirectory(crypto)
add_subdirectory(storage)
add_subdirectory(crdt)
add_subdirectory(subscription)
add_subdirectory(migration)
add_subdirectory(processing)
add_subdirectory(local_secure_storage)
add_subdirectory(singleton)
add_subdirectory(watcher)
add_subdirectory(coinprices)
add_subdirectory(proof)
add_subdirectory(multisig)
add_subdirectory(securecrdt)
add_subdirectory(trustedpeer)

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.h*"
)
