add_library(genius_prover
    GeniusProver.cpp
)
add_library(genius_assigner
    GeniusAssigner.cpp
)

target_link_libraries(genius_assigner
    LLVMIRReader
    LLVMCore
    LLVMSupport
)


supergenius_install(genius_assigner)
supergenius_install(genius_prover)