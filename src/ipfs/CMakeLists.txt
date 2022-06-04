add_library(ipfs
    ipfs_dht.cpp
    ipfs_dht.hpp
    )

target_include_directories(ipfs PRIVATE ${GSL_INCLUDE_DIR})

target_include_directories(ipfs
    PUBLIC $<BUILD_INTERFACE:${PROTO_SRC_DIR}>
   "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_link_libraries(ipfs
    ipfs-bitswap-cpp
    ${WIN_CRYPT_LIBRARY}
    )
