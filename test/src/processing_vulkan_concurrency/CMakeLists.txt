addtest(vulkan_init_concurrency_test
    vulkan_init_concurrency_test.cpp
)

target_include_directories(vulkan_init_concurrency_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(vulkan_init_concurrency_test
    nlohmann_json
    ProcessingBase
)

# BUILD-01/Phase 18: a real, fresh ctest run of this target (both TEST_F cases,
# including the new CreateSucceedsWithRealVulkanDevicePresent regression test)
# on a real-Vulkan-device host observed 2.99s wall-clock; TIMEOUT set to 4x that
# rounded-up value (3s -> 12s) so a future re-introduced VulkanInitMutex()
# re-entrancy deadlock fails fast as a ctest Timeout instead of hanging the suite.
set_tests_properties( vulkan_init_concurrency_test PROPERTIES TIMEOUT 12 )

add_custom_command(TARGET vulkan_init_concurrency_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
    "$<TARGET_FILE_DIR:vulkan_init_concurrency_test>/processing_datatypes"
    COMMENT "Creating processing_datatypes fixture directory"
)

add_custom_command(TARGET vulkan_init_concurrency_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/../processing_datatypes/bert-tiny.mnn"
    "${CMAKE_CURRENT_SOURCE_DIR}/../processing_datatypes/string-processing-definition.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/../processing_datatypes/test_input.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/../processing_datatypes/spleen_ct_seg.mnn"
    "${CMAKE_CURRENT_SOURCE_DIR}/../processing_datatypes/spleen_15.raw"
    "${CMAKE_CURRENT_SOURCE_DIR}/../processing_datatypes/texture3d-processing-definition.json"
    "$<TARGET_FILE_DIR:vulkan_init_concurrency_test>/processing_datatypes/"
    COMMENT "Copying fixture files from processing_datatypes"
)
