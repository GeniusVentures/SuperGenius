add_library(system_api_service
    system_jrpc_processor.cpp
    impl/system_api_impl.cpp
    )
target_link_libraries(system_api_service
    api_system_requests
    )

add_subdirectory(requests)
