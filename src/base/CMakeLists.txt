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

add_library(mp_utils
    mp_utils.cpp
    mp_utils.hpp
)
target_link_libraries(mp_utils
    PUBLIC
    Boost::headers
)
supergenius_install(mp_utils)

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

