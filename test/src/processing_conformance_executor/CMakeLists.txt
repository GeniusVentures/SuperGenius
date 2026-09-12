addtest(processing_conformance_executor_test
    executor_selection_test.cpp
)

target_include_directories(processing_conformance_executor_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(processing_conformance_executor_test
    nlohmann_json
    ProcessingBase
)
