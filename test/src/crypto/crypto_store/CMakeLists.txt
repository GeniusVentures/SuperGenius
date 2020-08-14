

addtest(key_type_test
    key_type_test.cpp
    )
target_link_libraries(key_type_test
    crypto_store_key_type
    )

addtest(crypto_store_test
    crypto_store_test.cpp
    )

set_target_properties(crypto_store_test PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")
if(WIN32)    
    target_link_libraries(crypto_store_test
    wsock32 ws2_32 userenv
    crypto_store
    base_fs_test
    )
else()
    target_link_libraries(crypto_store_test
    crypto_store
    base_fs_test
    )    
endif()
target_link_libraries(crypto_store_test
    crypto_store
    base_fs_test
    )
