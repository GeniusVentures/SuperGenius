cmake_minimum_required(VERSION 3.30)

project(evm_bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(EVM_BRIDGE_HEADERS
        rlp_encoder.hpp
        rlp_decoder.hpp
        evm_types.hpp)

set(EVM_BRIDGE_SOURCES
        rlp_encoder.cpp
        rlp_decoder.cpp
)

add_library(evm_bridge STATIC ${EVM_BRIDGE_HEADERS} ${EVM_BRIDGE_SOURCES})
target_include_directories(evm_bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
