add_library(api_state_requests
    get_storage.cpp
    get_runtime_version.cpp
    )

target_link_libraries(api_state_requests
    Boost::boost
    blob
    )
