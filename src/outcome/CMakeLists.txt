add_library(outcome INTERFACE)
set_target_properties(outcome PROPERTIES PUBLIC_HEADER "outcome.hpp")
target_link_libraries(outcome INTERFACE
    Boost::boost
    p2p::p2p_event
)
supergenius_install(outcome)
