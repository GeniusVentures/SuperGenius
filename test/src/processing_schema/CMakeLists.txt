addtest(processing_schema_test
    processing_schema_test.cpp
)

target_include_directories(processing_schema_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(processing_schema_test
    nlohmann_json
    ProcessingBase
)

add_custom_command(TARGET processing_schema_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/data/"
    "$<TARGET_FILE_DIR:processing_schema_test>/processing_schema/data"
    COMMENT "Copying data"
)
add_custom_command(TARGET processing_schema_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/model.mnn"
    "${CMAKE_CURRENT_SOURCE_DIR}/bad-processing-definition.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/gnus-processing-schema.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/missing-inputs-definition.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/posenet-processing-job.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/test-processing-definition.json"
    "$<TARGET_FILE_DIR:processing_schema_test>/processing_schema/"
    COMMENT "Copying files"
)
