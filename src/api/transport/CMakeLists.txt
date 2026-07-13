add_library(api_transport
    impl/ws/ws_client_impl.cpp
)

set_target_properties(api_transport PROPERTIES UNITY_BUILD ON)

target_link_libraries(api_transport
    PUBLIC
    Boost::headers
    logger
    outcome
    OpenSSL::SSL
)

# Add /bigobj flag for MSVC compiler specifically for api_transport
if (MSVC)
    target_compile_options(api_transport PRIVATE /bigobj)
endif()

supergenius_install(api_transport)
