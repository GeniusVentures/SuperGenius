add_subdirectory(api/transport)
add_subdirectory(account)
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
add_subdirectory(peerregistry)
add_subdirectory(trustedpeer)
add_subdirectory(networkregistry)

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.h*"
)
