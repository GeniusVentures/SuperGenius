addtest(processing_conformance_cancellation_test
    cancellation_conformance_test.cpp
)

target_include_directories(processing_conformance_cancellation_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(processing_conformance_cancellation_test
    nlohmann_json
    ProcessingBase
)
