

addtest(sr25519_provider_test
    sr25519_provider_test.cpp
    )
if(WIN32)
    target_link_libraries(sr25519_provider_test wsock32 ws2_32 sr25519_provider)
else()
    target_link_libraries(sr25519_provider_test    sr25519_provider  )
endif()


