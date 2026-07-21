
addtest(price_retrieval_test
    price_retrieval_test.cpp
    )

# Any test that calls the live CoinGecko API must use this lock.
set_tests_properties(price_retrieval_test PROPERTIES RESOURCE_LOCK coingecko_api)


target_link_libraries(price_retrieval_test
    rapidjson
    OpenSSL::SSL
    coinprices
)
