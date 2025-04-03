add_library(hasher
    hasher/hasher_impl.cpp
)
target_link_libraries(hasher
    PRIVATE
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
target_link_libraries(sr25519_types
    PUBLIC
    blob
    sr25519-donna
    Boost::headers
)
supergenius_install(sr25519_types)

add_library(ed25519_types
    ed25519_types.cpp
    ed25519_types.hpp
)
target_link_libraries(ed25519_types
    PUBLIC
    blob
    iroha::ed25519
)
supergenius_install(ed25519_types)

add_library(vrf_provider
    vrf/vrf_provider_impl.cpp
)
target_link_libraries(vrf_provider
    PUBLIC
    sr25519_types
    buffer
    p2p::p2p_random_generator # generator from libp2p
)

add_library(sr25519_provider
    sr25519/sr25519_provider_impl.cpp
)
target_link_libraries(sr25519_provider
    PUBLIC
    sr25519_types
    PRIVATE
    p2p::p2p_random_generator # generator from libp2p
)
supergenius_install(sr25519_provider)

add_library(ed25519_provider
    ed25519/ed25519_provider_impl.cpp
)
target_link_libraries(ed25519_provider
    PUBLIC
    outcome
    ed25519_types
)
supergenius_install(ed25519_provider)

add_library(secp256k1_provider
    secp256k1/secp256k1_provider_impl.cpp
)
target_link_libraries(secp256k1_provider
    PUBLIC
    libsecp256k1::secp256k1
    outcome
    blob
)
supergenius_install(
    secp256k1_provider
)

add_library(pbkdf2_provider
    pbkdf2/impl/pbkdf2_provider_impl.cpp
)
target_link_libraries(pbkdf2_provider
    PUBLIC
    buffer
    outcome
    PRIVATE
    OpenSSL::Crypto
)
supergenius_install(pbkdf2_provider)

add_subdirectory(bip39)
add_subdirectory(crypto_store)
add_subdirectory(keccak)
add_subdirectory(sha)
add_subdirectory(twox)
