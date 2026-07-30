addtest(processing_dispatch_test
    processing_dispatch_test.cpp
)

target_include_directories(processing_dispatch_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(processing_dispatch_test
    nlohmann_json
    ProcessingBase
)

add_custom_command(TARGET processing_dispatch_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
    "$<TARGET_FILE_DIR:processing_dispatch_test>/processing_dispatch/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/render-pass-valid-definition.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/render-pass-missing-shader-definition.json"
    "$<TARGET_FILE_DIR:processing_dispatch_test>/processing_dispatch/"
    COMMENT "Copying dispatch test fixtures"
)
