

add_library(vote_graph
    vote_graph_impl.cpp
    )
target_link_libraries(vote_graph
    Boost::boost
    vote_weight
    )
