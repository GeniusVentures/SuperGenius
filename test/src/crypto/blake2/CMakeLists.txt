addtest(blake2_test
    blake2_test.cpp
    )
target_link_libraries(blake2_test
    ipfs-lite-cpp::blake2
    hexutil
)
