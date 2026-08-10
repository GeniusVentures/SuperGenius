addtest(securecrdt_interface_test
    securecrdt_interface_test.cpp
)
target_link_libraries(securecrdt_interface_test
    securecrdt
)

addtest(securecrdt_registry_test
    securecrdt_registry_test.cpp
)
target_link_libraries(securecrdt_registry_test
    securecrdt
)

set(SECURECRDT_TEST_NODE_LIBS
    securecrdt
    crdt_globaldb
    multisig
    sgns_genius_account
    json_secure_storage
)

addtest(securecrdt_quorum_gate_test
    securecrdt_quorum_gate_test.cpp
)
target_link_libraries(securecrdt_quorum_gate_test
    ${SECURECRDT_TEST_NODE_LIBS}
)

addtest(securecrdt_propose_sign_quorum_test
    securecrdt_propose_sign_quorum_test.cpp
)
target_link_libraries(securecrdt_propose_sign_quorum_test
    ${SECURECRDT_TEST_NODE_LIBS}
)

addtest(securecrdt_quorum_contract_e2e_test
    securecrdt_quorum_contract_e2e_test.cpp
)
target_link_libraries(securecrdt_quorum_contract_e2e_test
    ${SECURECRDT_TEST_NODE_LIBS}
)
