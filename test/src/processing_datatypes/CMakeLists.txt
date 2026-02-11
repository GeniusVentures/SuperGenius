addtest(processing_datatypes_test
    processing_datatypes_test.cpp
)

target_include_directories(processing_datatypes_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(processing_datatypes_test
    nlohmann_json
    ProcessingBase
)

add_custom_command(TARGET processing_datatypes_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
    "$<TARGET_FILE_DIR:processing_datatypes_test>/processing_datatypes"
    COMMENT "Creating processing_datatypes directory"
)

add_custom_command(TARGET processing_datatypes_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/bert-tiny.mnn"
    "${CMAKE_CURRENT_SOURCE_DIR}/string-processing-definition.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_input.txt"
    "$<TARGET_FILE_DIR:processing_datatypes_test>/processing_datatypes/"
    COMMENT "Copying test files"
)
