add_library(processing_service
    processing_core.hpp
    impl/processing_core_impl.hpp
    impl/processing_core_impl.cpp
    processing_imagesplit.hpp
    processing_imagesplit.cpp
    processing_tasksplit.hpp
    processing_tasksplit.cpp
    processing_processor.hpp
    processors/processing_processor_mnn_posenet.hpp
    processors/processing_processor_mnn_posenet.cpp
    processing_engine.hpp
    processing_engine.cpp
    processing_node.hpp
    processing_node.cpp
    processing_service.hpp
    processing_service.cpp
    processing_subtask_enqueuer.hpp
    processing_subtask_enqueuer_impl.hpp
    processing_subtask_enqueuer_impl.cpp
    processing_subtask_queue.hpp
    processing_subtask_queue.cpp
    processing_subtask_queue_accessor.hpp
    processing_subtask_queue_accessor_impl.hpp
    processing_subtask_queue_accessor_impl.cpp
    processing_subtask_queue_channel.hpp
    processing_subtask_queue_channel_pubsub.hpp
    processing_subtask_queue_channel_pubsub.cpp
    processing_subtask_queue_manager.hpp
    processing_subtask_queue_manager.cpp
    processing_subtask_result_storage.hpp
    impl/processing_subtask_result_storage_impl.hpp
    impl/processing_subtask_result_storage_impl.cpp
    processing_subtask_state_storage.hpp
    processing_task_queue.hpp
    impl/processing_task_queue_impl.hpp
    impl/processing_task_queue_impl.cpp
    processing_validation_core.cpp
    processing_validation_core.hpp
)

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
        target_link_libraries(processing_service PRIVATE
            "-framework AppKit")
    endif()
endif()

target_link_libraries(processing_service PRIVATE
    crdt_globaldb
    ipfs-pubsub
    SGProcessingProto
    OpenSSL::Crypto
    OpenSSL::SSL
    Boost::Boost.DI
    p2p::p2p_basic_host
    p2p::p2p_default_network
    p2p::p2p_peer_repository
    p2p::p2p_inmem_address_repository
    p2p::p2p_inmem_key_repository
    p2p::p2p_inmem_protocol_repository
    p2p::p2p_gossip
    p2p::asio_scheduler
    ${WIN_CRYPT_LIBRARY}
    ${VULKAN_LIBS}
    AsyncIOManager
    rapidjson
    ipfs-pubsub
)
target_link_libraries(processing_service PUBLIC
    MNN::MNN)
target_include_directories(processing_service PRIVATE ${AsyncIOManager_INCLUDE_DIR})

#if(WIN32)
#    target_link_options(processing_service PUBLIC
#        /WHOLEARCHIVE
#        MNN::MNN
#    )
#elseif(APPLE)
#    target_link_options(processing_service PUBLIC
#        -force_load
#        MNN::MNN
#    )
#else()
#    target_link_options(processing_service PUBLIC
#        "-Wl,--whole-archive"
#        MNN::MNN
#        "-Wl,--no-whole-archive"
#    )
#endif()

supergenius_install(processing_service)
