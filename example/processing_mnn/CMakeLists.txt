#Find MNN Stuff
set(MNN_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/MNN/include")
set(MNN_LIBRARY_DIR "${_THIRDPARTY_BUILD_DIR}/MNN/lib")
include_directories(${MNN_INCLUDE_DIR})
set(MNN_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../MNN/include")
FILE(GLOB MNN_LIBS "${MNN_LIBRARY_DIR}/*")

add_executable(processing_mnn
    processing_mnn.cpp
	processing_task_queue_impl.hpp
    )


# Copy the required headers to install directory
include_directories(
    ${PROJECT_SOURCE_DIR}/src
    )

target_include_directories(processing_mnn PRIVATE ${GSL_INCLUDE_DIR}) 


target_link_libraries(processing_mnn PRIVATE
#    ipfs-lite-cpp::ipfs_datastore_rocksdb
#    ipfs-lite-cpp::buffer
#    ipfs-lite-cpp::ipld_node
#    ipfs-lite-cpp::ipfs_merkledag_service
#    ipfs-lite-cpp::graphsync
    logger
	processing_service
	crdt_globaldb
    p2p::p2p_basic_host
    p2p::p2p_default_network
    p2p::p2p_peer_repository
    p2p::p2p_inmem_address_repository
    p2p::p2p_inmem_key_repository
    p2p::p2p_inmem_protocol_repository
    p2p::p2p_literals
    p2p::p2p_kademlia
    p2p::p2p_identify
    p2p::p2p_ping
    Boost::Boost.DI
    Boost::program_options
    ipfs-bitswap-cpp
	ipfs-pubsub
    ${WIN_CRYPT_LIBRARY}
	${MNN_LIBS}
    )
include_directories(../include)
include_directories(./imageHelper)