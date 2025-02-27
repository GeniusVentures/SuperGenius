add_library(coinprices
    coinprices.cpp 
    coinprices.hpp
)

target_include_directories(coinprices PRIVATE ${AsyncIOManager_INCLUDE_DIR})
target_link_libraries(coinprices
    PUBLIC
    rapidjson
    OpenSSL::SSL
    AsyncIOManager
    logger
    outcome
)

supergenius_install(coinprices)
