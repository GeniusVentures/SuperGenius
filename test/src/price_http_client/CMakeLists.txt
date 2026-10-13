addtest(price_http_client_test
    price_http_client_test.cpp
)

target_include_directories(price_http_client_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})
target_link_libraries(price_http_client_test
    price_test_support
    Boost::headers
    OpenSSL::SSL
    AsyncIOManager
)
