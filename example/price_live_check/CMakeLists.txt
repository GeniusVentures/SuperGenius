add_executable(price_live_check price_live_check.cpp)
target_include_directories(price_live_check PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${AsyncIOManager_INCLUDE_DIR}
)
target_link_libraries(price_live_check PRIVATE
    coinprices
    Boost::headers
    OpenSSL::SSL
    AsyncIOManager
)
