add_library(processing_service
    processing_core.hpp
	impl/processing_core_impl.hpp
	impl/processing_core_impl.cpp
	processing_imagesplit.hpp
	processing_processor.hpp
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
    processing_subtask_state_storage.hpp
    processing_task_queue.hpp
    processing_validation_core.cpp
    processing_validation_core.hpp
    )


add_proto_library(SGProcessingProto proto/SGProcessing.proto)

target_include_directories(processing_service PRIVATE ${GSL_INCLUDE_DIR})

target_include_directories(processing_service
    PUBLIC $<BUILD_INTERFACE:${PROTO_SRC_DIR}>
   "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_link_libraries(processing_service
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
    )
