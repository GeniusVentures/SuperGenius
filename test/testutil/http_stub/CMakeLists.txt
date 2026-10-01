add_library(price_test_support STATIC
    HttpStubServer.cpp
)

target_include_directories(price_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(price_test_support
    PUBLIC
    Boost::headers
)
disable_clang_tidy(price_test_support)
