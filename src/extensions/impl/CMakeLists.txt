add_library(crypto_extension
    crypto_extension.cpp
    )
set_target_properties(crypto_extension PROPERTIES PUBLIC_HEADER "crypto_extension.hpp")
target_link_libraries(crypto_extension
    bip39_provider
    hasher
    logger
    p2p::p2p_random_generator
    sr25519_provider
    secp256k1_provider
    ed25519_provider
    scale
    crypto_store
    )
supergenius_install(crypto_extension)

add_library(io_extension
    io_extension.cpp
    )
set_target_properties(io_extension PROPERTIES PUBLIC_HEADER "io_extension.hpp")
target_link_libraries(io_extension
    hexutil
    logger
    buffer
    )
supergenius_install(io_extension)

add_library(memory_extension
    memory_extension.cpp
    )
set_target_properties(memory_extension PROPERTIES PUBLIC_HEADER "memory_extension.hpp")
target_link_libraries(memory_extension
    logger
    buffer
)
supergenius_install(memory_extension)

add_library(misc_extension
    misc_extension.cpp
    )
set_target_properties(misc_extension PROPERTIES PUBLIC_HEADER "misc_extension.hpp")
target_link_libraries(misc_extension
    scale
    logger
    const_wasm_provider
    outcome
    blob
    )
supergenius_install(misc_extension)

add_library(storage_extension
    storage_extension.cpp
    )
set_target_properties(storage_extension PROPERTIES PUBLIC_HEADER "storage_extension.hpp")
target_link_libraries(storage_extension
    buffer
    blob
    logger
    ordered_trie_hash
    )
supergenius_install(storage_extension)
