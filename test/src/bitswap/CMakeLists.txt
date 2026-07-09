add_library(bitswap_test_stubs STATIC
    stubs.cpp
)

target_include_directories(bitswap_test_stubs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(bitswap_test_stubs PUBLIC
    ipfs-bitswap-cpp
    p2p::p2p_basic_host
    p2p::p2p_cid
    Boost::headers
    GTest::gtest_main
    GTest::gmock_main
)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
)

addtest(concurrency_cache_dir_test
    concurrency_cache_dir_test.cpp
)
target_link_libraries(concurrency_cache_dir_test
    bitswap_test_stubs
    ipfs-bitswap-cpp
)

addtest(concurrency_config_test
    concurrency_config_test.cpp
)
target_link_libraries(concurrency_config_test
    bitswap_test_stubs
    ipfs-bitswap-cpp
)

addtest(concurrency_publish_test
    concurrency_publish_test.cpp
)
target_link_libraries(concurrency_publish_test
    bitswap_test_stubs
    ipfs-bitswap-cpp
)

addtest(concurrency_get_block_test
    concurrency_get_block_test.cpp
)
target_link_libraries(concurrency_get_block_test
    bitswap_test_stubs
    ipfs-bitswap-cpp
)

addtest(concurrency_content_request_test
    concurrency_content_request_test.cpp
)
target_link_libraries(concurrency_content_request_test
    bitswap_test_stubs
    ipfs-bitswap-cpp
)

addtest(concurrency_request_context_test
    concurrency_request_context_test.cpp
)
target_link_libraries(concurrency_request_context_test
    bitswap_test_stubs
    ipfs-bitswap-cpp
)

addtest(concurrency_callback_test
    concurrency_callback_test.cpp
)
target_link_libraries(concurrency_callback_test
    bitswap_test_stubs
    ipfs-bitswap-cpp
)

# Combined stress test
addtest(concurrency_stress_test
    concurrency_stress_test.cpp
)
target_link_libraries(concurrency_stress_test
    bitswap_test_stubs
    ipfs-bitswap-cpp
)
