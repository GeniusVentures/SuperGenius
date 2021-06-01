add_library(processing_service
    processing_room.cpp
    processing_node.cpp
    processing_service.cpp
    processing_engine.cpp
    processing_shared_queue.cpp
    processing_subtask_queue.cpp
    )


message("${CMAKE_CURRENT_SOURCE_DIR}/proto/SGProcessing.proto")

set(PROTO_SRC_DIR ${CMAKE_CURRENT_BINARY_DIR}/proto)
file(MAKE_DIRECTORY ${PROTO_SRC_DIR})

# Generating of C++ code from .proto
protobuf_generate(
    LANGUAGE cpp
    TARGET processing_service
    PROTOS "${CMAKE_CURRENT_SOURCE_DIR}/proto/SGProcessing.proto"
    IMPORT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/proto"
    PROTOC_OUT_DIR "${PROTO_SRC_DIR}"
    OUT_VAR _PROTOC_RESULT
    )


target_include_directories(processing_service PRIVATE ${GSL_INCLUDE_DIR})

target_include_directories(processing_service 
    PUBLIC $<BUILD_INTERFACE:${PROTO_SRC_DIR}>
   "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_link_libraries(processing_service
    ipfs-pubsub
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
