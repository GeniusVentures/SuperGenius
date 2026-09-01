set(NETWORKREGISTRY_TEST_NODE_LIBS
    networkregistry
    trustedpeer
    securecrdt
    crdt_globaldb
    multisig
    json_secure_storage
    libsecp256k1::secp256k1
    sgns_genius_account
)

addtest(network_registry_test
    network_registry_test.cpp
)
target_link_libraries(network_registry_test
    ${NETWORKREGISTRY_TEST_NODE_LIBS}
)
target_include_directories(network_registry_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
