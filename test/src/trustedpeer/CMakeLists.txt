set(TRUSTEDPEER_TEST_NODE_LIBS
    trustedpeer
    securecrdt
    crdt_globaldb
    multisig
    json_secure_storage
    libsecp256k1::secp256k1
    sgns_genius_account
)

addtest(genesis_manifest_test
    genesis_manifest_test.cpp
)
target_link_libraries(genesis_manifest_test
    trustedpeer
)

addtest(quorum_policy_test
    quorum_policy_test.cpp
)
target_link_libraries(quorum_policy_test
    trustedpeer
    multisig
    sgns_genius_account
)

addtest(trustedpeerregistry_genesis_test
    trustedpeerregistry_genesis_test.cpp
)
target_link_libraries(trustedpeerregistry_genesis_test
    ${TRUSTEDPEER_TEST_NODE_LIBS}
)
target_include_directories(trustedpeerregistry_genesis_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

addtest(trustedpeerregistry_quorum_test
    trustedpeerregistry_quorum_test.cpp
)
target_link_libraries(trustedpeerregistry_quorum_test
    ${TRUSTEDPEER_TEST_NODE_LIBS}
)
target_include_directories(trustedpeerregistry_quorum_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

addtest(trustedpeerregistry_threshold_floor_test
    trustedpeerregistry_threshold_floor_test.cpp
)
target_link_libraries(trustedpeerregistry_threshold_floor_test
    ${TRUSTEDPEER_TEST_NODE_LIBS}
)
target_include_directories(trustedpeerregistry_threshold_floor_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
