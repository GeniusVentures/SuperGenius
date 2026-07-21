addtest(multisig_verify_test
    multisig_verify_test.cpp
)
target_link_libraries(multisig_verify_test
    multisig
    json_secure_storage
)
