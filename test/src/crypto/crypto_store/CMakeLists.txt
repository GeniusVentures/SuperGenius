
addtest(key_type_test
    key_type_test.cpp
    )
target_link_libraries(key_type_test
    crypto_store_key_type
    )

addtest(crypto_store_test
    crypto_store_test.cpp
    )

if(FORCE_MULTILE)
  set_target_properties(crypto_store_test PROPERTIES LINK_FLAGS "${MULTIPLE_OPTION}")
endif()
  
target_link_libraries(crypto_store_test
    ${USER_ENV_LIBRARY}
    ${WIN_SOCK_LIBRARY}
    ${WIN_CRYPT_LIBRARY}
    crypto_store
    base_fs_test
    )
