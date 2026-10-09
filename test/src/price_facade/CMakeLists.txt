addtest(price_facade_test
    price_facade_test.cpp
)

target_include_directories(price_facade_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})
target_link_libraries(price_facade_test
    coinprices
    price_test_support
    rapidjson
    Boost::headers
    OpenSSL::SSL
    AsyncIOManager
)
