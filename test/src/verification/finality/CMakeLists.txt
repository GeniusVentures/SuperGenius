
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
    )

