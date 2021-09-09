message("graphsync_app Boost_LIBRARIES: ${Boost_LIBRARIES}")
add_executable(graphsync_app
    graphsync_app.cpp
    graphsync_acceptance_common.cpp
    )
target_link_libraries(graphsync_app
    ipfs-lite-cpp::graphsync
    ipfs-lite-cpp::ipfs_merkledag_service
    p2p::asio_scheduler
    Boost::program_options
    ${WIN_CRYPT_LIBRARY}
    )
