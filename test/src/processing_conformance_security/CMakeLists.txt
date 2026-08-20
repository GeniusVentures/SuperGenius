addtest(processing_conformance_security_test
    secv01_counter_test.cpp
    secv01_tex3d_counter_test.cpp
    secv01_render_lighting_counter_test.cpp
    secv01_render_blending_counter_test.cpp
    secv01_render_texturing_counter_test.cpp
    secv02_counter_test.cpp
)

target_include_directories(processing_conformance_security_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(processing_conformance_security_test
    nlohmann_json
    ProcessingBase
    processing_service
    ipfs-bitswap-cpp
    logger
    Boost::headers
    p2p::p2p_logger
    ipfs-pubsub
)

# This suite's JSON job definitions are all inline string literals (not
# on-disk PatchedJson() fixtures), so the only files that need to reach the
# test binary's output directory are Task 1's two SECV-01 counter-test
# fixtures, referenced by nested "file://processing_conformance_security/fixtures/..."
# URIs -- mirroring processing_conformance_regression/CMakeLists.txt's
# second (nested-path) copy_directory command.
add_custom_command(TARGET processing_conformance_security_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/"
    "$<TARGET_FILE_DIR:processing_conformance_security_test>/processing_conformance_security/fixtures/"
)
