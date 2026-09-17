addtest(processing_conformance_regression_test
    regression_test.cpp
)

target_include_directories(processing_conformance_regression_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(processing_conformance_regression_test
    nlohmann_json
    ProcessingBase
)

add_custom_command(TARGET processing_conformance_regression_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/"
    "$<TARGET_FILE_DIR:processing_conformance_regression_test>/fixtures/"
)

# Several fixture JSON literals (in this suite, in processing_conformance_schema, and in
# processing_conformance_capability) embed shader source URIs of the literal form
# "file://processing_conformance_regression/fixtures/passthrough.*.spv" — i.e. the JSON
# author expected the URI's relative path to resolve directly under the shared test_bin
# output directory, not under the flat "fixtures/" copy above. This second copy places the
# same fixture files at that exact nested path so ProcessingManager::Process()'s real file
# load (via FileManager's "file" scheme, which treats the URI's path component literally)
# can find them. Discovered while wiring RenderConformanceTest to actually call Process()
# (09-11 Task 2) — previously latent because no test ever loaded the shader bytes from disk.
add_custom_command(TARGET processing_conformance_regression_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/"
    "$<TARGET_FILE_DIR:processing_conformance_regression_test>/processing_conformance_regression/fixtures/"
)
