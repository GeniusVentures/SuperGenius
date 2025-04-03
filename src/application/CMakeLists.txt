add_library(app_state_manager
    impl/app_state_manager_impl.cpp
)
target_link_libraries(app_state_manager
    PUBLIC
    logger
)

add_library(app_config_impl
    impl/app_config_impl.cpp
)
target_link_libraries(app_config_impl
    Boost::program_options
)

add_library(local_key_storage
    impl/config_reader/error.cpp
    impl/local_key_storage.cpp
)
target_link_libraries(local_key_storage
    PUBLIC
    Boost::filesystem
    sr25519_types
    ed25519_types
    p2p::p2p_crypto_key
)

add_library(configuration_storage
    impl/configuration_storage_impl.cpp
)
target_link_libraries(configuration_storage
    PUBLIC
    Boost::headers
    logger
    p2p::p2p_peer_id
    primitives
    PRIVATE
    sr25519_types
)

add_library(validating_node_application
    impl/validating_node_application.cpp
)
target_link_libraries(validating_node_application
    PUBLIC
    local_key_storage
    blob
)

if(FORCE_MULTIPLE)
    set_target_properties(validating_node_application PROPERTIES LINK_FLAGS "${MULTIPLE_OPTION}")
endif()
