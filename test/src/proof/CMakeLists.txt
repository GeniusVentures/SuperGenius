
addtest(proof_generation_test
    ProofAssignerTest.cpp
    )
target_link_libraries(proof_generation_test
    genius_assigner
    Boost::boost
    Boost::log
    Boost::random
    Boost::system
    )

