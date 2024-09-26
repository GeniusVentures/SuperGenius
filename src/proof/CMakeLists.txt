add_library(genius_prover
    GeniusProver.cpp
)
add_library(genius_assigner
    GeniusAssigner.cpp
)

target_link_libraries(genius_assigner
    outcome
    Boost::boost
    LLVMIRReader
    LLVMCore
    LLVMSupport
)
target_link_libraries(genius_prover
    genius_assigner
    outcome
    Boost::boost
)

supergenius_install(genius_assigner)
supergenius_install(genius_prover)