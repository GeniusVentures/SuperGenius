add_library(keccak
    keccak.c
    keccak.h
)
disable_clang_tidy(keccak)
supergenius_install(keccak)
