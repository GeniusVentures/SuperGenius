addtest(processing_dispatch_test
    processing_dispatch_test.cpp
)

set_tests_properties( processing_dispatch_test PROPERTIES WORKING_DIRECTORY "$<TARGET_FILE_DIR:processing_dispatch_test>" )

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
    "${CMAKE_CURRENT_SOURCE_DIR}/dummy_shader.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/dummy_fragment_shader.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/malformed_vertex_shader.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/invalid_spirv_bytes.spv"
    "${CMAKE_CURRENT_SOURCE_DIR}/render-pass-malformed-glsl-definition.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/render-pass-invalid-spirv-definition.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/render-pass-legacy-hlsl-definition.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/scalar_position_vertex_shader.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/solid_red_fragment_shader.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/render-pass-happy-path-definition.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/texturing-source-image.raw"
    "${CMAKE_CURRENT_SOURCE_DIR}/lighting-fixture-definition.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/lighting_vertex_shader.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/lighting_fragment_shader.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/lighting-vertex-data.raw"
    "$<TARGET_FILE_DIR:processing_dispatch_test>/processing_dispatch/"
    COMMENT "Copying dispatch test fixtures"
)
