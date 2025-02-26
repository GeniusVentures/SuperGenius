add_library(coinprices
    coinprices.cpp 
    coinprices.hpp
)

target_include_directories(coinprices PRIVATE )
target_link_libraries(coinprices
    PUBLIC
    rapidjson
    OpenSSL::SSL
)

supergenius_install(coinprices)
