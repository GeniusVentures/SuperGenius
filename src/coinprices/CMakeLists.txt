add_library(coinprices
    coinprices.cpp 
)

target_include_directories(coinprices PRIVATE ${AsyncIOManager_INCLUDE_DIR})
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
