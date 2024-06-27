
add_library(hasher
    hasher/hasher_impl.cpp
    )
set_target_properties(hasher PROPERTIES PUBLIC_HEADER "hasher/hasher_impl.hpp")
target_link_libraries(hasher
    ipfs-lite-cpp::blake2
    twox
    buffer
    sha
    keccak
    )
supergenius_install(hasher)

add_library(sr25519_types
    sr25519_types.cpp
    )
set_target_properties(sr25519_types PROPERTIES PUBLIC_HEADER "sr25519_types.hpp")
target_link_libraries(sr25519_types
    mp_utils
    blob
    sr25519-donna
    )
supergenius_install(sr25519_types)

add_library(ed25519_types
    ed25519_types.cpp
    ed25519_types.hpp
    )
set_target_properties(ed25519_types PROPERTIES PUBLIC_HEADER "ed25519_types.hpp")
target_link_libraries(ed25519_types
    blob
    sgns::ed25519
    )
supergenius_install(ed25519_types)

add_library(vrf_provider
    vrf/vrf_provider_impl.cpp
    )
set_target_properties(vrf_provider PROPERTIES PUBLIC_HEADER "vrf/vrf_provider_impl.hpp")
target_link_libraries(vrf_provider
    mp_utils
    sr25519_types
    buffer
    blob
    p2p::p2p_random_generator # generator from libp2p
    )

add_library(sr25519_provider
    sr25519/sr25519_provider_impl.cpp
    )
set_target_properties(sr25519_provider PROPERTIES PUBLIC_HEADER "sr25519/sr25519_provider_impl.hpp")
target_link_libraries(sr25519_provider
    p2p::p2p_random_generator # generator from libp2p
    sr25519_types
    )
supergenius_install(sr25519_provider)

add_library(ed25519_provider
    ed25519/ed25519_provider_impl.cpp
    )
set_target_properties(ed25519_provider PROPERTIES PUBLIC_HEADER "ed25519/ed25519_provider_impl.hpp")
target_link_libraries(ed25519_provider
    p2p::p2p_random_generator # generator from libp2p
    ed25519_types
    )
supergenius_install(ed25519_provider)

add_library(secp256k1_provider
    secp256k1/secp256k1_provider_impl.cpp
    )
set_target_properties(secp256k1_provider PROPERTIES PUBLIC_HEADER "secp256k1/secp256k1_provider_impl.hpp")
target_link_libraries(secp256k1_provider PUBLIC
    OpenSSL::Crypto
    PUBLIC OpenSSL::SSL
    blob
    buffer
    secp256k1::secp256k1
    scale
    )
supergenius_install(
    secp256k1_provider
)

add_library(pbkdf2_provider
    pbkdf2/impl/pbkdf2_provider_impl.cpp
    )
set_target_properties(pbkdf2_provider PROPERTIES PUBLIC_HEADER "pbkdf2/pbkdf2_provider.hpp;pbkdf2/impl/pbkdf2_provider_impl.hpp")
target_link_libraries(pbkdf2_provider
    PUBLIC OpenSSL::SSL
    OpenSSL::Crypto
    buffer
    blob
    )
supergenius_install(pbkdf2_provider)

add_subdirectory(bip39)
add_subdirectory(crypto_store)
add_subdirectory(keccak)
add_subdirectory(sha)
add_subdirectory(twox)
