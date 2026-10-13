addtest(price_manager_test
    price_manager_test.cpp
)

# PriceFetchError.hpp / PriceRetryPolicy.hpp pull <HTTPTypes.hpp> — the
# AsyncIOManager include dir is required even though this suite links NO
# transport (zero sockets: no price_test_support, no HttpStubServer, no
# AsyncIOManager in target_link_libraries — TEST-02).
target_include_directories(price_manager_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})
target_link_libraries(price_manager_test
    coinprices
    Boost::headers
)
