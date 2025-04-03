add_library(api_transport
    impl/http/http_session.cpp
    impl/ws/ws_session.cpp
    error.cpp
    impl/http/http_listener_impl.cpp
    impl/ws/ws_listener_impl.cpp
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

add_library(rpc_thread_pool
    rpc_io_context.hpp
    rpc_thread_pool.hpp
    rpc_thread_pool.cpp
)
target_link_libraries(rpc_thread_pool
    PUBLIC
    Boost::headers
    logger
)
