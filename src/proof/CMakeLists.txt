add_proto_library(SGProofProto proto/SGProof.proto)

add_library(genius_prover
    GeniusProver.cpp
)
add_library(genius_assigner
    GeniusAssigner.cpp
)
add_library(basic_proof
    IBasicProof.cpp
)
add_library(transfer_proof
    TransferProof.cpp
)
add_library(processing_proof
    ProcessingProof.cpp
)
# add_library(recursive_transfer_proof
#     RecursiveTransferProof.cpp
# )

target_link_libraries(genius_assigner
    outcome
    Boost::boost
    Boost::log
    Boost::json
    LLVMIRReader
    LLVMCore
    LLVMSupport
)
target_link_libraries(genius_prover
    genius_assigner
    outcome
    Boost::boost
)
if(MSVC)
    target_compile_options(genius_prover PUBLIC /constexpr:steps1500000 /bigobj)
    target_compile_options(genius_assigner PUBLIC /constexpr:steps1500000 /bigobj)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(genius_prover PUBLIC -fconstexpr-steps=10000000)
endif()
target_link_libraries(basic_proof
    genius_assigner
    genius_prover
    outcome
    SGProofProto
)
target_link_libraries(transfer_proof
    genius_assigner
    genius_prover
    basic_proof
    SGProofProto
    outcome
    Boost::boost
    Boost::log
    Boost::unit_test_framework
)
# target_link_libraries(recursive_transfer_proof
#     transfer_proof
#     genius_assigner
#     genius_prover
#     basic_proof
#     SGProofProto
#     outcome
#     Boost::boost
#     Boost::log
#     Boost::unit_test_framework
# )
target_link_libraries(processing_proof
    basic_proof
    SGProofProto
    outcome
    Boost::boost
    Boost::log
    Boost::unit_test_framework
)

supergenius_install(genius_assigner)
supergenius_install(genius_prover)
supergenius_install(transfer_proof)
supergenius_install(processing_proof)
supergenius_install(basic_proof)