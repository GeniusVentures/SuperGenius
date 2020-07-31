# add_library(app_state_manager
#     impl/app_state_manager_impl.cpp
#     )
# target_link_libraries(app_state_manager
#     logger
#     )
add_library(local_key_storage
    impl/local_key_storage.cpp
    )
target_link_libraries(local_key_storage
    primitives
    config_reader_error
    Boost::filesystem
    sr25519_types
    ed25519_types
    p2p::p2p_crypto_key
    )