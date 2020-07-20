add_library(voter_set
    voter_set.cpp
    )
target_link_libraries(voter_set
    ed25519_types
    )