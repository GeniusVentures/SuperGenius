addtest(processing_conformance_hashing_test
    output_hashing_test.cpp
)

target_include_directories(processing_conformance_hashing_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(processing_conformance_hashing_test
    nlohmann_json
    ProcessingBase
)
