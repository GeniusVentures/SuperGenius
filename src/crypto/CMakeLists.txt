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

add_subdirectory(keccak)
add_subdirectory(sha)
add_subdirectory(twox)
