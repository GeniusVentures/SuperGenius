addtest(processing_conformance_capability_test
    capability_conformance_test.cpp
)

target_include_directories(processing_conformance_capability_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(processing_conformance_capability_test
    nlohmann_json
    ProcessingBase
)
