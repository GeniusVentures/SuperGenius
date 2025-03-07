add_proto_library(SGProofProto proto/SGProof.proto)

add_library(genius_prover
    GeniusAssigner.cpp
    GeniusProver.cpp
)
target_link_libraries(genius_prover
    outcome
    Boost::boost
    Boost::log
    Boost::json
    LLVMIRReader
    LLVMCore
    LLVMSupport
)
set_target_properties(genius_prover PROPERTIES UNITY_BUILD ON)

add_library(basic_proof
    IBasicProof.cpp
)
target_link_libraries(basic_proof
    genius_prover
    outcome
    SGProofProto
)

add_library(transfer_proof
    TransferProof.cpp
)
target_link_libraries(transfer_proof
    basic_proof
    Boost::unit_test_framework
)

add_library(processing_proof
    ProcessingProof.cpp
)
target_link_libraries(processing_proof
    basic_proof
    Boost::unit_test_framework
)

# add_library(recursive_transfer_proof
#     RecursiveTransferProof.cpp
# )
if(MSVC)
    target_compile_options(genius_prover PUBLIC /constexpr:steps1500000 /bigobj)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(genius_prover PUBLIC -fconstexpr-steps=10000000)
endif()

# target_link_libraries(recursive_transfer_proof
#     transfer_proof
#     genius_prover
#     basic_proof
#     SGProofProto
#     outcome
#     Boost::boost
#     Boost::log
#     Boost::unit_test_framework
# )
supergenius_install(genius_prover)
supergenius_install(transfer_proof)
supergenius_install(processing_proof)
supergenius_install(basic_proof)