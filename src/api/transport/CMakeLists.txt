add_library(api_transport
    impl/http/http_session.hpp
    impl/http/http_session.cpp
    impl/ws/ws_session.hpp
    impl/ws/ws_session.cpp
    error.hpp
    error.cpp
    listener.hpp
    session.hpp
    impl/http/http_listener_impl.hpp
    impl/http/http_listener_impl.cpp
    impl/ws/ws_listener_impl.hpp
    impl/ws/ws_listener_impl.cpp
    impl/ws/ws_client_impl.hpp
    impl/ws/ws_client_impl.cpp
)
target_link_libraries(api_transport
    PUBLIC
    Boost::headers
    logger
    outcome
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
