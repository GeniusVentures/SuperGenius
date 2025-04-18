add_executable(sgns_node
    entry.cpp
    app_delegate.cpp

    # node.cpp
    # ipfs_lite_store.hpp
    # ipfs_lite_store.cpp
    # node_config.cpp
    # network.cpp
    # cli.cpp
    # common.cpp
    # logging.cpp
    # node_rpc_config.cpp
)

target_link_libraries(sgns_node
    PRIVATE
    Boost::system
    Boost::thread
    Boost::filesystem
    ipfs_blockservice
    block_producing_node_application
    validating_node_application
    app_config_impl
    protobuf::libprotobuf
    ${USER_ENV_LIBRARY} # On windows platform , sr25519 library needs extra library userenv, so folllowing code is added
    ${WIN_CRYPT_LIBRARY}
)

if(FORCE_MULTIPLE)
    set_target_properties(sgns_node PROPERTIES LINK_FLAGS ${MULTIPLE_OPTION})
endif()

supergenius_install(sgns_node)
