
addtest(prover_test
    ProverTest.cpp
)
target_link_libraries(prover_test
    genius_prover
    Boost::boost
    Boost::log
    Boost::random
    Boost::system
    Boost::unit_test_framework
)

addtest(genius_proofs
    GeniusProofsTest.cpp
)
target_link_libraries(genius_proofs
    transfer_proof
    processing_proof
    basic_proof
    genius_prover
    Boost::boost
    Boost::log
    Boost::random
    Boost::system
    Boost::unit_test_framework
)
