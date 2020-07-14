

add_library(outcome INTERFACE)
target_link_libraries(outcome INTERFACE
    Boost::boost
    )
supergenius_install(outcome)
