
addtest(price_retrieval_test
    price_retrieval_test.cpp
    )


target_link_libraries(price_retrieval_test
    rapidjson
    OpenSSL::SSL
    coinprices
)
