

add_executable(processing_mnn
    processing_mnn.cpp
    processing_mnn.hpp
)

add_executable(processing_mnn_p
    processing_mnn_p.cpp
    processing_mnn.hpp
)

# Copy the required headers to install directory
include_directories(
    ${PROJECT_SOURCE_DIR}/src
)

target_include_directories(processing_mnn PRIVATE ${GSL_INCLUDE_DIR})

target_include_directories(processing_mnn_p PRIVATE ${GSL_INCLUDE_DIR})

if(APPLE)
    set(XCFRAMEWORK_PATH "${_THIRDPARTY_BUILD_DIR}/moltenvk/build/lib/MoltenVK.xcframework")
    target_link_libraries(processing_mnn PRIVATE
        "-framework CoreFoundation"
        "-framework CoreGraphics"
        "-framework CoreServices"
        "-framework IOKit"
        "-framework IOSurface"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework AppKit"
#		${XCFRAMEWORK_PATH} 
    )
endif()

target_link_libraries(processing_mnn PRIVATE
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
    AsyncIOManager
    rapidjson
)

target_include_directories(
    processing_mnn
    PRIVATE
    ${AsyncIOManager_INCLUDE_DIR}
)

if(APPLE)
    set(XCFRAMEWORK_PATH "${_THIRDPARTY_BUILD_DIR}/moltenvk/build/lib/MoltenVK.xcframework")
    target_link_libraries(processing_mnn_p PRIVATE
        "-framework CoreFoundation"
        "-framework CoreGraphics"
        "-framework CoreServices"
        "-framework IOKit"
        "-framework IOSurface"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework AppKit"
#		${XCFRAMEWORK_PATH} 
    )
endif()

target_link_libraries(processing_mnn_p PRIVATE
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
)
if(WIN32)
    target_link_options(processing_mnn PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:processing_service>)
    target_link_options(processing_mnn_p PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:processing_service>)
elseif(APPLE)
    target_link_options(processing_mnn PUBLIC -force_load "$<TARGET_FILE:processing_service>")
    target_link_options(processing_mnn_p PUBLIC -force_load "$<TARGET_FILE:processing_service>")
else()
    target_link_options(processing_mnn PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:processing_service>"
        "-Wl,--no-whole-archive"
    )
    target_link_options(processing_mnn_p PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:processing_service>"
        "-Wl,--no-whole-archive"
    )
endif()
include_directories(../include)
include_directories(./imageHelper)
