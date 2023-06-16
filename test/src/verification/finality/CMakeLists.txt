
addtest(vote_tracker_test
    vote_tracker_test.cpp	
    )
target_link_libraries(vote_tracker_test
    vote_tracker	
    )

addtest(voting_round_test
    voting_round_test.cpp	
    )
target_link_libraries(voting_round_test
    voting_round
    vote_weight
    vote_tracker
    vote_graph
    clock
    )

addtest(vote_crypto_provider_test
    vote_crypto_provider_test.cpp	
    )
target_link_libraries(vote_crypto_provider_test
    vote_crypto_provider	
    ed25519_provider
    )

addtest(vote_graph_test
    vote_graph_test.cpp	
    )
target_link_libraries(vote_graph_test
    vote_graph	
    )
