add_library(coinprices
    coinprices.cpp
    PriceRetryPolicy.cpp
    PriceHttpClient.cpp
)

target_include_directories(coinprices PRIVATE ${AsyncIOManager_INCLUDE_DIR})

# Pinned Mozilla CA bundle (D-07): compile-time default path for TLS verify_peer;
# a constructor parameter overrides at runtime.
target_compile_definitions(coinprices PRIVATE
    SGNS_DEFAULT_CACERT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/certs/cacert.pem"
)

target_link_libraries(coinprices
    PUBLIC
    Boost::headers
    logger
    outcome
    PRIVATE
    rapidjson
    AsyncIOManager
)

supergenius_install(coinprices)
