add_library(hexutil
    hexutil.hpp
    hexutil.cpp
)
target_link_libraries(hexutil
    PUBLIC
    outcome
)
supergenius_install(hexutil)

add_library(buffer
    buffer.hpp
    buffer.cpp
)
target_link_libraries(buffer
    PUBLIC
    Boost::headers
    outcome
    PRIVATE
    hexutil
)
supergenius_install(buffer)

add_library(blob
    blob.hpp
    blob.cpp
)
target_link_libraries(blob
    PUBLIC
    hexutil
    Boost::headers
)
supergenius_install(blob)

add_library(logger
    logger.hpp
    logger.cpp
)
target_link_libraries(logger
    PUBLIC
    spdlog::spdlog
)
supergenius_install(logger)

add_library(ScaledInteger
    ScaledInteger.cpp
    ScaledInteger.hpp
)
target_link_libraries(ScaledInteger
    PUBLIC
    Boost::headers
    outcome
)
supergenius_install(ScaledInteger)

message(STATUS "Generating sgnsv.h from sgnsv.h.in")

cmake_policy(SET CMP0115 NEW)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sgnsv.h.in")
    message(FATAL_ERROR "sgnsv.h.in is missing in ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated/base)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/sgnsv.h.in
    ${CMAKE_BINARY_DIR}/generated/base/sgnsv.h
    @ONLY
)

if(NOT EXISTS "${CMAKE_BINARY_DIR}/generated/base/sgnsv.h")
    message(FATAL_ERROR "Error: Generated file ${CMAKE_BINARY_DIR}/generated/base/sgnsv.h not found!")
endif()

add_library(sgns_version
    sgns_version.cpp
    sgns_version.hpp
    ${CMAKE_BINARY_DIR}/generated/base/sgnsv.h
)

target_include_directories(sgns_version
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated/>
    $<INSTALL_INTERFACE:include>
)

supergenius_install(sgns_version)


# Header-only application-layer gossip payload authentication (CR-G01):
# SealGossipPayload / OpenGossipPayload bind an envelope-embedded public key
# to the PeerId derived from the gossip from-field and verify a signature over
# the payload before any membership predicate is consulted.
add_library(gossip_auth INTERFACE)
supergenius_install(gossip_auth)
target_link_libraries(gossip_auth
    INTERFACE
    p2p::p2p_crypto_provider
    p2p::p2p_key_marshaller
    p2p::p2p_key_validator
    p2p::p2p_ed25519_provider
    p2p::p2p_random_generator
    p2p::p2p_rsa_provider
    p2p::p2p_ecdsa_provider
    p2p::p2p_secp256k1_provider
    p2p::p2p_hmac_provider
    p2p::p2p_peer_id
)
