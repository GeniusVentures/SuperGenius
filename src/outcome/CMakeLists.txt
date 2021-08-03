

add_library(outcome INTERFACE)
set_target_properties(outcome PROPERTIES PUBLIC_HEADER "outcome.hpp")
target_link_libraries(outcome INTERFACE
    Boost::boost
    )
supergenius_install(outcome)
