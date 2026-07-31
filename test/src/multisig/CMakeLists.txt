addtest(multisig_verify_test
    multisig_verify_test.cpp
)
target_link_libraries(multisig_verify_test
    multisig
    sgns_genius_account
    json_secure_storage
)

addtest(multisig_quorum_test
    multisig_quorum_test.cpp
)
target_link_libraries(multisig_quorum_test
    multisig
    sgns_genius_account
    json_secure_storage
)
