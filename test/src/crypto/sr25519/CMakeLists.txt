

addtest(sr25519_provider_test
    sr25519_provider_test.cpp
    )
target_link_libraries(
    sr25519_provider_test 
    ${WIN_SOCK_LIBRARY}
    sr25519_provider
    ${USER_ENV_LIBRARY}
    )


