set(TRUSTEDPEER_TEST_NODE_LIBS
    trustedpeer
    securecrdt
    crdt_globaldb
    multisig
    json_secure_storage
)

addtest(trustedpeerregistry_genesis_test
    trustedpeerregistry_genesis_test.cpp
)
target_link_libraries(trustedpeerregistry_genesis_test
    ${TRUSTEDPEER_TEST_NODE_LIBS}
)

addtest(trustedpeerregistry_quorum_test
    trustedpeerregistry_quorum_test.cpp
)
target_link_libraries(trustedpeerregistry_quorum_test
    ${TRUSTEDPEER_TEST_NODE_LIBS}
)
