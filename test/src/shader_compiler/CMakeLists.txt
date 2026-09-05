addtest(shader_compiler_test
    shader_compiler_test.cpp
)

target_link_libraries(shader_compiler_test
    SGShaderCompiler
    Boost::filesystem
)

# boost/dll.hpp (program_location) references dlerror/dlopen; on glibc < 2.34
# (e.g. the debian-bullseye CI container) those live in libdl. Nothing else
# this test links pulls -ldl transitively, so link it explicitly.
# CMAKE_DL_LIBS is empty on Windows/macOS, where the symbols resolve implicitly.
target_link_libraries(shader_compiler_test
    PRIVATE
    ${CMAKE_DL_LIBS}
)

add_custom_command(TARGET shader_compiler_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
    "$<TARGET_FILE_DIR:shader_compiler_test>/shader_compiler/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/valid_vertex.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/valid_fragment.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/malformed.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/invalid.spv"
    "$<TARGET_FILE_DIR:shader_compiler_test>/shader_compiler/"
    COMMENT "Copying shader_compiler test fixtures"
)
