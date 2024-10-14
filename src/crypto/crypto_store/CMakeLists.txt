

add_library(crypto_store_key_type
    key_type.cpp
    )
set_target_properties(crypto_store_key_type PROPERTIES PUBLIC_HEADER "key_type.hpp")
target_link_libraries(crypto_store_key_type
    Boost::boost
    outcome
)
supergenius_install(crypto_store_key_type)

add_library(crypto_store
    crypto_store_impl.cpp
    )
set_target_properties(crypto_store PROPERTIES PUBLIC_HEADER "crypto_store_impl.hpp")
target_link_libraries(crypto_store
    Boost::filesystem
    ed25519_provider
    sr25519_provider
    secp256k1_provider
    p2p::p2p_random_generator # generator from libp2p
    bip39_provider
    crypto_store_key_type
    )
supergenius_install(crypto_store)
