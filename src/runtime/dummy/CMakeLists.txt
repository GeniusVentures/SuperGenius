add_library(finality_api_dummy
    finality_api_dummy.cpp
)
target_link_libraries(finality_api_dummy
    PUBLIC
    sr25519_types
    ed25519_types
    p2p::p2p_crypto_key
)
