add_library(proof_generator
    GeniusAssigner.cpp
)

target_link_libraries(proof_generator
    LLVMIRReader
    LLVMCore
    LLVMSupport
)


supergenius_install(proof_generator)