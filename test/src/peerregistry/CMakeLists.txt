set(PEERREGISTRY_TEST_NODE_LIBS
    trustedpeer
    securecrdt
    peerregistry
    crdt_globaldb
    multisig
    json_secure_storage
    libsecp256k1::secp256k1
    sgns_genius_account
)

addtest(peer_registry_test
    peer_registry_test.cpp
)
target_link_libraries(peer_registry_test
    ${PEERREGISTRY_TEST_NODE_LIBS}
)
target_include_directories(peer_registry_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
