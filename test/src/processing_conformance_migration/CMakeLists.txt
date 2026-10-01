addtest(processing_conformance_migration_test
    migration_adapter_test.cpp
)

target_include_directories(processing_conformance_migration_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(processing_conformance_migration_test
    nlohmann_json
    ProcessingBase
)
