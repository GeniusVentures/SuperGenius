

add_executable (sgns_node
    entry.cpp
    app_delegate.cpp
    node.cpp
    ipfs_lite_store.hpp
    ipfs_lite_store.cpp
    # node_config.cpp
    # network.cpp
    cli.cpp
    # common.cpp
    # logging.cpp
    # node_rpc_config.cpp
    )

target_link_libraries(sgns_node
    blocklattice::secure
    blocklattice::common_lib
    blocklattice::cryptopp
    Boost::system
    Boost::thread
    Boost::filesystem
    )

install(TARGETS sgns_node)