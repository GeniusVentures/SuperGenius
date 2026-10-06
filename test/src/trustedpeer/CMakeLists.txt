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

addtest(trust_genesis_tool_test
    trust_genesis_tool_test.cpp
)
target_link_libraries(trust_genesis_tool_test
    ${TRUSTEDPEER_TEST_NODE_LIBS}
    local_trust_admin
    burnconfig
    rocksdb
    OpenSSL::Crypto
)
target_include_directories(trust_genesis_tool_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_dependencies(trust_genesis_tool_test sgns-trust)

addtest(quorum_policy_test
    quorum_policy_test.cpp
)

addtest(trust_state_store_test
    trust_state_store_test.cpp
)

addtest(operator_approval_test
    operator_approval_test.cpp
)
target_link_libraries(operator_approval_test
    ${TRUSTEDPEER_TEST_NODE_LIBS}
    rocksdb
)
target_include_directories(operator_approval_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(trust_state_store_test
    trustedpeer
    sgns_genius_account
    rocksdb
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

# Runtime-class labels name the infrastructure a test binary needs, never its
# domain (same grain as requires_foundry in bridge_e2e). CI and local runs select
# or exclude classes via `ctest -L` / `ctest -LE` with no test-tree churn:
#   inprocess_node = constructs in-process GlobalDB/libp2p nodes (MakeSecureCrdtTestNode)
#   inprocess_db   = temp-directory RocksDB only, no node
# genesis_manifest_test and quorum_policy_test deliberately carry no label: they
# are pure value tests needing no disk or network infrastructure.
set_tests_properties(
    operator_approval_test
    trust_genesis_tool_test
    trustedpeerregistry_genesis_test
    trustedpeerregistry_quorum_test
    trustedpeerregistry_threshold_floor_test
    PROPERTIES LABELS "inprocess_node"
)
set_tests_properties(
    trust_state_store_test
    PROPERTIES LABELS "inprocess_db"
)
