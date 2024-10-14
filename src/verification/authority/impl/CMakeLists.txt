
add_library(authority_manager
    authority_manager_impl.cpp
    schedule_node.cpp
    )
target_link_libraries(authority_manager
    logger
    authority_manager_error
    authority_update_observer_error
    sr25519_types
)
