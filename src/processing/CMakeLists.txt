add_library(processing_service
    impl/processing_core_impl.cpp
    processing_tasksplit.cpp
    processing_engine.cpp
    processing_node.cpp
    processing_service.cpp
    processing_subtask_enqueuer_impl.cpp
    processing_subtask_queue.cpp
    processing_subtask_queue_accessor_impl.cpp
    processing_subtask_queue_channel_pubsub.cpp
    processing_subtask_queue_manager.cpp
    processing_subtask_result_storage.hpp
    impl/processing_subtask_result_storage_impl.cpp
    impl/processing_task_queue_impl.cpp
    processing_validation_core.cpp
    processing_validation_core.hpp
)

set_target_properties(processing_service PROPERTIES UNITY_BUILD ON)

add_proto_library(SGProcessingProto proto/SGProcessing.proto)

target_include_directories(processing_service PRIVATE ${GSL_INCLUDE_DIR})

target_include_directories(processing_service
    PUBLIC $<BUILD_INTERFACE:${PROTO_SRC_DIR}>
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

if(APPLE)
    set(XCFRAMEWORK_PATH "${_THIRDPARTY_BUILD_DIR}/moltenvk/build/lib/MoltenVK.xcframework")
    target_link_libraries(processing_service PRIVATE
        "-framework CoreFoundation"
        "-framework CoreGraphics"
        "-framework CoreServices"
        "-framework IOKit"
        "-framework IOSurface"
        "-framework Metal"
        "-framework QuartzCore"

        # ${XCFRAMEWORK_PATH}
        "-framework Foundation"
    )

    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        # macOS specific
        target_link_libraries(processing_service PRIVATE
            "-framework AppKit")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        # iOS specific
        target_link_libraries(processing_service PRIVATE
            "-framework UIKit")
    endif()
endif()

target_link_libraries(processing_service
    PUBLIC
    MNN::MNN
    Vulkan::Vulkan
	ProcessingBase
    PRIVATE
    crdt_globaldb
    ipfs-pubsub
    SGProcessingProto
    ${WIN_CRYPT_LIBRARY}
    AsyncIOManager
    rapidjson
    sha
)

target_include_directories(processing_service PRIVATE ${AsyncIOManager_INCLUDE_DIR})

supergenius_install(processing_service)
