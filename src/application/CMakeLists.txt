add_library(app_state_manager
    impl/app_state_manager_impl.cpp
    )
target_link_libraries(app_state_manager
    logger
    )