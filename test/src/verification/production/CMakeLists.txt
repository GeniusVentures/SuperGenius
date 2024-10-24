

#addtest(production_test
#    production_test.cpp
#    )
#target_link_libraries(production_test
#    production
#    clock
#    sr25519_types
#    sr25519_provider
#    ${USER_ENV_LIBRARY}
#    )

addtest(threshold_util_test
    threshold_util_test.cpp
    )
target_link_libraries(threshold_util_test
    threshold_util
    )
