
file(GLOB h_files
            "*.hpp"
    )
add_library(bip39_provider
    dictionary.cpp
    entropy_accumulator.cpp
    impl/bip39_provider_impl.cpp
    mnemonic.cpp
    ${h_files}
    )
set_target_properties(bip39_provider PROPERTIES PUBLIC_HEADER "${h_files}")
target_link_libraries(bip39_provider
    PUBLIC OpenSSL::SSL
    OpenSSL::Crypto
    buffer
    blob
    logger
    pbkdf2_provider
    sha
    )
supergenius_install(bip39_provider)
