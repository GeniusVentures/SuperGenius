addtest(processing_conformance_schema_test
    schema_validation_test.cpp
)

target_include_directories(processing_conformance_schema_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(processing_conformance_schema_test
    nlohmann_json
    ProcessingBase
)

add_custom_command(TARGET processing_conformance_schema_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/"
    "$<TARGET_FILE_DIR:processing_conformance_schema_test>/fixtures/"
)
