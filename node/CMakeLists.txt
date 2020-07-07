

add_executable (sgns_node
    entry.cpp
    app_delegate.cpp
    node.cpp
    ipfs_lite_store.cpp
    node_config.cpp
    network.cpp
    )

target_link_libraries(sgns_node
    blocklattice::secure
    blocklattice::common_lib
    blocklattice::cryptopp
    Boost::system
    Boost::thread
    Boost::filesystem
    )