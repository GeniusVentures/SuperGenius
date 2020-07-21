add_subdirectory(vote_graph)
add_library(voter_set
    voter_set.cpp
    )
target_link_libraries(voter_set
    ed25519_types
    )
add_library(vote_weight
    vote_weight.cpp
    )
target_link_libraries(vote_weight
    voter_set
    )

add_library(environment
    impl/environment_impl.cpp
    )

target_link_libraries(environment
    scale
    logger
    blob
    )