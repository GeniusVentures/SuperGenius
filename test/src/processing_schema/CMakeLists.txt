
addtest(processing_schema_test
    processing_schema_test.cpp
)

target_include_directories(processing_schema_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(processing_schema_test
    valijson
	nlohmann_json
	ProcessingSchema::ProcessingSchema
)
#wow