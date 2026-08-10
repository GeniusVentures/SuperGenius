
# Live CoinGecko smoke test: build it, but do not register it with CTest.
add_executable(price_retrieval_test
    price_retrieval_test.cpp
    )

target_link_libraries(price_retrieval_test
    GTest::gtest_main
    rapidjson
    OpenSSL::SSL
    coinprices
)

set_target_properties(price_retrieval_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test_bin
)
disable_clang_tidy(price_retrieval_test)
