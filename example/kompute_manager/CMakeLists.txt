# Copy the required headers to install directory
include_directories(
    ${PROJECT_SOURCE_DIR}/src
    )


#kompute_dapp_processor

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DKOMPUTE_DISABLE_VK_DEBUG_LAYERS=1")

add_executable(kompute_dapp_processor
    kompute_manager.cpp
    kompute_manager.hpp
    infura_ipfs.hpp
    infura_ipfs.cpp
    kompute_dapp_processor.cpp
    processing_subtask_result_storage_impl.cpp
    processing_subtask_result_storage_impl.hpp
    )


target_include_directories(kompute_dapp_processor PRIVATE ${GSL_INCLUDE_DIR})


target_link_libraries(kompute_dapp_processor
    OpenSSL::Crypto
    OpenSSL::SSL
    p2p::p2p_crypto_error
    p2p::p2p_aes_provider
    logger
    crdt_globaldb
    processing_service
    kompute::kompute
    vulkan::vulkan # @note When it is on top of the list SGProcessing.pb.h cannot be found.
    Boost::program_options    
    #p2p::p2p_basic_host
    #p2p::p2p_default_network
    #p2p::p2p_peer_repository
    #p2p::p2p_inmem_address_repository
    #p2p::p2p_inmem_key_repository
    #p2p::p2p_inmem_protocol_repository
    #p2p::p2p_literals
    #p2p::p2p_kademlia
    #p2p::p2p_identify
    #p2p::p2p_ping
    #Boost::Boost.DI
    ${WIN_CRYPT_LIBRARY}
    )

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/authorizationKey.txt")
    add_custom_command(TARGET kompute_dapp_processor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/authorizationKey.txt"
        $<TARGET_FILE_DIR:kompute_dapp_processor>
    )
endif()

set_property(TARGET kompute_dapp_processor PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:kompute_dapp_processor>")

# kompute_dapp
add_executable(kompute_dapp
    infura_ipfs.hpp
    infura_ipfs.cpp
    kompute_dapp.cpp
    )

target_link_libraries(kompute_dapp
    kompute::kompute
    #vulkan::vulkan-headers
    #vulkan::vulkan # @note When it is on top of the list SGProcessing.pb.h cannot be found.
    OpenSSL::Crypto
    OpenSSL::SSL
    p2p::p2p_crypto_error
    p2p::p2p_aes_provider
    crdt_globaldb
    logger
    processing_service
    ${WIN_CRYPT_LIBRARY}
    )

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/authorizationKey.txt")
    add_custom_command(TARGET kompute_dapp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/authorizationKey.txt"
        $<TARGET_FILE_DIR:kompute_dapp>
    )
endif()


if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/array_multiplication.comp.spv")
    add_custom_command(TARGET kompute_dapp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/array_multiplication.comp.spv"
        $<TARGET_FILE_DIR:kompute_dapp>
    )
endif()

set_property(TARGET kompute_dapp PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:kompute_dapp>")

