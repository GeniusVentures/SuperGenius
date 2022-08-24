set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DKOMPUTE_DISABLE_VK_DEBUG_LAYERS=1")

add_executable(kompute_manager
    #kompute_manager.cpp
    #kompute_manager.hpp
    infura_ipfs.hpp
    infura_ipfs.cpp
    main.cpp
    )


# Copy the required headers to install directory
include_directories(
    ${PROJECT_SOURCE_DIR}/src
    )

target_include_directories(kompute_manager PRIVATE ${GSL_INCLUDE_DIR})


target_link_libraries(kompute_manager
    kompute::kompute
    vulkan::vulkan
    logger
    ipfs
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
    OpenSSL::Crypto
    OpenSSL::SSL
    p2p::p2p_crypto_error
    p2p::p2p_aes_provider
    ${WIN_CRYPT_LIBRARY}
    )

#install(FILES authorizationKey.txt DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/authorizationKey.txt")
    add_custom_command(TARGET kompute_manager POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/authorizationKey.txt"
        $<TARGET_FILE_DIR:kompute_manager>
    )
    set_property(TARGET kompute_manager PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:kompute_manager>")
endif()