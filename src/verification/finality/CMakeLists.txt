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
    ed25519_types
)

add_library(vote_tracker
    impl/vote_tracker_impl.cpp
    )
target_link_libraries(vote_tracker
    blob
    ed25519_types
)

add_library(vote_crypto_provider
    impl/vote_crypto_provider_impl.cpp
    )
target_link_libraries(vote_crypto_provider
    scale
    ed25519_types
)

add_library(voting_round_error
    impl/voting_round_error.cpp
    )
target_link_libraries(voting_round_error
    outcome
    )

add_library(voting_round
    impl/voting_round_impl.cpp
    )

target_link_libraries(voting_round
    logger
    voter_set
    voting_round_error
    )

add_library(finality
    impl/finality_impl.cpp
    )
target_link_libraries(finality
    voting_round
    vote_crypto_provider
    vote_graph
    vote_tracker
    )

add_library(syncing_round_observer
    impl/syncing_round_observer.cpp
    )
target_link_libraries(syncing_round_observer
    logger
    Boost::boost
    outcome
    ed25519_types
)

add_library(finalization_composite
    impl/finalization_composite.cpp
    )
target_link_libraries(finalization_composite
    logger
    outcome
)
