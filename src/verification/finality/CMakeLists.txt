add_subdirectory(vote_graph)
add_library(voter_set
    voter_set.cpp
)
target_link_libraries(voter_set
    PUBLIC
    primitives
    clock
    Boost::headers
    ed25519_types
)
add_library(vote_weight
    vote_weight.cpp
)
target_link_libraries(vote_weight
    PUBLIC
    Boost::headers
    buffer
    ed25519_types
)

add_library(environment
    impl/environment_impl.cpp
)

target_link_libraries(environment
    PUBLIC
    logger
    Boost::headers
    ed25519_types
    PRIVATE
    scale
)

add_library(vote_tracker
    impl/vote_tracker_impl.cpp
)
target_link_libraries(vote_tracker
    PUBLIC
    Boost::headers
    buffer
    ed25519_types
)

add_library(vote_crypto_provider
    impl/vote_crypto_provider_impl.cpp
)
target_link_libraries(vote_crypto_provider
    PUBLIC
    voter_set
    ed25519_provider
    Boost::headers
    PRIVATE
    scale
)

add_library(voting_round_error
    impl/voting_round_error.cpp
)
target_link_libraries(voting_round_error
    PUBLIC
    outcome
)

add_library(voting_round
    impl/voting_round_impl.cpp
)

target_link_libraries(voting_round
    PUBLIC
    Boost::headers
    logger
    ed25519_types
    PRIVATE
    voting_round_error
)

add_library(syncing_round_observer
    impl/syncing_round_observer.cpp
)
target_link_libraries(syncing_round_observer
    PUBLIC
    logger
    Boost::headers
    buffer
    ed25519_types
)

add_library(finality
    impl/finality_impl.cpp
)
target_link_libraries(finality
    PUBLIC
    logger
    ed25519_provider
    PRIVATE
    Boost::headers
    scale
    syncing_round_observer
)

add_library(finalization_composite
    impl/finalization_composite.cpp
)
target_link_libraries(finalization_composite
    PUBLIC
    outcome
)
