add_library(api_system_requests
    name.cpp
    version.cpp
    chain.cpp
    properties.cpp
    )

target_link_libraries(api_system_requests
    Boost::boost
    )
