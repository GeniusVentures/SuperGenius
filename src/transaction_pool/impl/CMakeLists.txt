
add_library(pool_moderator
    pool_moderator_impl.cpp
    )
target_link_libraries(pool_moderator
    Boost::boost
    clock
    )
