
add_library(state_api_service
    state_jrpc_processor.cpp
    impl/state_api_impl.cpp
    )
target_link_libraries(state_api_service
    api_state_requests
    buffer
    api_service
    trie_storage
    blob
    )

add_subdirectory(requests)
