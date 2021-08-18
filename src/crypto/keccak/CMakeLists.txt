

add_library(keccak
        keccak.c
        keccak.h
        )
set_target_properties(keccak PROPERTIES PUBLIC_HEADER "keccak.h")
target_link_libraries(keccak
        OpenSSL::Crypto
        buffer
        blob
        )
disable_clang_tidy(keccak)
supergenius_install(keccak)
