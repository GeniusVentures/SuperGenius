# BOOST VERSION TO USE
set(BOOST_MAJOR_VERSION "1" CACHE STRING "Boost Major Version")
set(BOOST_MINOR_VERSION "85" CACHE STRING "Boost Minor Version")
set(BOOST_PATCH_VERSION "0" CACHE STRING "Boost Patch Version")

# convenience settings
set(BOOST_VERSION "${BOOST_MAJOR_VERSION}.${BOOST_MINOR_VERSION}.${BOOST_PATCH_VERSION}")
set(BOOST_VERSION_3U "${BOOST_MAJOR_VERSION}_${BOOST_MINOR_VERSION}_${BOOST_PATCH_VERSION}")
set(BOOST_VERSION_2U "${BOOST_MAJOR_VERSION}_${BOOST_MINOR_VERSION}")

# --------------------------------------------------------
# Set config of GTest
set(BUILD_TESTING "ON" CACHE BOOL "Build tests")

add_definitions(-D_USE_INSTALLED_BOOST_JSON_=TRUE)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DDEBUG_BYTECODE_CIRCUITS)
    add_definitions(-DSGNS_DEBUG)
else()
    add_definitions(-DRELEASE_BYTECODE_CIRCUITS)
endif()

if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
	add_definitions(-DSGNS_DEBUGLOGS)
elseif (DEFINED SGNS_PRINT_LOGS)
	add_definitions(-DSGNS_DEBUGLOGS)
endif()

set(ZLIB_DIR "${_THIRDPARTY_BUILD_DIR}/zlib/lib/cmake/zlib")
find_package(ZLIB CONFIG REQUIRED)

if(BUILD_TESTING)
    set(GTest_DIR "${_THIRDPARTY_BUILD_DIR}/GTest/lib/cmake/GTest")
    message("Gtest dir: ${GTest_DIR}")
    set(GTest_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/GTest/include")
    find_package(GTest CONFIG REQUIRED)
endif()

# absl
if(NOT DEFINED absl_DIR)
    set(absl_DIR "${_THIRDPARTY_BUILD_DIR}/protobuf/lib/cmake/absl")
endif()

# utf8_range
if(NOT DEFINED utf8_range_DIR)
    set(utf8_range_DIR "${_THIRDPARTY_BUILD_DIR}/protobuf/lib/cmake/utf8_range")
endif()

# protobuf project
if(NOT DEFINED Protobuf_DIR)
    set(Protobuf_DIR "${_THIRDPARTY_BUILD_DIR}/protobuf/lib/cmake/protobuf")
endif()

if(NOT DEFINED grpc_INCLUDE_DIR)
    set(grpc_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/grpc/include")
endif()

if(NOT DEFINED Protobuf_INCLUDE_DIR)
    set(Protobuf_INCLUDE_DIR "${grpc_INCLUDE_DIR}/google/protobuf")
endif()

find_package(Protobuf CONFIG REQUIRED)

if(NOT DEFINED PROTOC_EXECUTABLE)
    set(PROTOC_EXECUTABLE "${_THIRDPARTY_BUILD_DIR}/protobuf/bin/protoc${CMAKE_EXECUTABLE_SUFFIX}")
endif()

set(Protobuf_PROTOC_EXECUTABLE ${PROTOC_EXECUTABLE} CACHE PATH "Initial cache" FORCE)

if(NOT TARGET protobuf::protoc)
    add_executable(protobuf::protoc IMPORTED)
endif()

if(EXISTS "${Protobuf_PROTOC_EXECUTABLE}")
    set_target_properties(protobuf::protoc PROPERTIES
        IMPORTED_LOCATION ${Protobuf_PROTOC_EXECUTABLE})
endif()

# protoc definition
get_target_property(PROTOC_LOCATION protobuf::protoc IMPORTED_LOCATION)
print("PROTOC_LOCATION: ${PROTOC_LOCATION}")

if(Protobuf_FOUND)
    message(STATUS "Protobuf version : ${Protobuf_VERSION}")
    message(STATUS "Protobuf compiler : ${Protobuf_PROTOC_EXECUTABLE}")
endif()

include(${PROJECT_ROOT}/cmake/functions.cmake)

# MNN
set(MNN_DIR "${_THIRDPARTY_BUILD_DIR}/MNN/lib/cmake/MNN")
find_package(MNN CONFIG REQUIRED)
set(MNN_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/MNN/include")
message(STATIS "INCLUDE DIR ${MNN_INCLUDE_DIR}")
include_directories(${MNN_INCLUDE_DIR})
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    get_target_property(MNN_LIB_PATH MNN::MNN IMPORTED_LOCATION_DEBUG)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    get_target_property(MNN_LIB_PATH MNN::MNN IMPORTED_LOCATION_RELEASE)
elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    get_target_property(MNN_LIB_PATH MNN::MNN IMPORTED_LOCATION_RELWITHDEBINFO)
endif()

# OpenSSL
set(OpenSSL_DIR "${_THIRDPARTY_BUILD_DIR}/openssl/build/lib/cmake/OpenSSL" CACHE PATH "Path to OpenSSL install folder")
set(OPENSSL_ROOT_DIR "${_THIRDPARTY_BUILD_DIR}/openssl/build" CACHE PATH "Path to OpenSSL install root folder")
set(OPENSSL_USE_STATIC_LIBS ON CACHE BOOL "OpenSSL use static libs")
set(OPENSSL_MSVC_STATIC_RT ON CACHE BOOL "OpenSSL use static RT")
set(OPENSSL_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/openssl/build/include" CACHE PATH "Path to OpenSSL include folder")

find_package(OpenSSL REQUIRED CONFIG)

# snappy
set(Snappy_DIR "${_THIRDPARTY_BUILD_DIR}/snappy/lib/cmake/Snappy")
find_package(Snappy CONFIG REQUIRED)

# rocksdb
set(RocksDB_DIR "${_THIRDPARTY_BUILD_DIR}/rocksdb/lib/cmake/rocksdb")
find_package(RocksDB CONFIG REQUIRED)

# stb
include_directories(${_THIRDPARTY_BUILD_DIR}/stb/include)

# Microsoft.GSL
set(GSL_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/Microsoft.GSL/include")
include_directories(${GSL_INCLUDE_DIR})

# fmt
set(fmt_DIR "${_THIRDPARTY_BUILD_DIR}/fmt/lib/cmake/fmt")
find_package(fmt CONFIG REQUIRED)

# spdlog v1.4.2
set(spdlog_DIR "${_THIRDPARTY_BUILD_DIR}/spdlog/lib/cmake/spdlog")
find_package(spdlog CONFIG REQUIRED)
add_compile_definitions("SPDLOG_FMT_EXTERNAL")

# soralog
set(soralog_DIR "${_THIRDPARTY_BUILD_DIR}/soralog/lib/cmake/soralog")
find_package(soralog CONFIG REQUIRED)

# yaml-cpp
set(yaml-cpp_DIR "${_THIRDPARTY_BUILD_DIR}/yaml-cpp/lib/cmake/yaml-cpp")
find_package(yaml-cpp CONFIG REQUIRED)

# tsl_hat_trie
set(tsl_hat_trie_DIR "${_THIRDPARTY_BUILD_DIR}/tsl_hat_trie/lib/cmake/tsl_hat_trie")
find_package(tsl_hat_trie CONFIG REQUIRED)

# Boost.DI
set(Boost.DI_DIR "${_THIRDPARTY_BUILD_DIR}/Boost.DI/lib/cmake/Boost.DI")
find_package(Boost.DI CONFIG REQUIRED)

# Boost should be loaded before libp2p v0.1.2
# Boost project
set(_BOOST_ROOT "${_THIRDPARTY_BUILD_DIR}/boost/build")
set(Boost_LIB_DIR "${_BOOST_ROOT}/lib")
set(Boost_INCLUDE_DIR "${_BOOST_ROOT}/include/boost-${BOOST_VERSION_2U}")
set(Boost_DIR "${Boost_LIB_DIR}/cmake/Boost-${BOOST_VERSION}")
set(boost_atomic_DIR "${Boost_LIB_DIR}/cmake/boost_atomic-${BOOST_VERSION}")
set(boost_chrono_DIR "${Boost_LIB_DIR}/cmake/boost_chrono-${BOOST_VERSION}")
set(boost_container_DIR "${Boost_LIB_DIR}/cmake/boost_container-${BOOST_VERSION}")
set(boost_context_DIR "${Boost_LIB_DIR}/cmake/boost_context-${BOOST_VERSION}")
set(boost_date_time_DIR "${Boost_LIB_DIR}/cmake/boost_date_time-${BOOST_VERSION}")
set(boost_filesystem_DIR "${Boost_LIB_DIR}/cmake/boost_filesystem-${BOOST_VERSION}")
set(boost_headers_DIR "${Boost_LIB_DIR}/cmake/boost_headers-${BOOST_VERSION}")
set(boost_json_DIR "${Boost_LIB_DIR}/cmake/boost_json-${BOOST_VERSION}")
set(boost_log_DIR "${Boost_LIB_DIR}/cmake/boost_log-${BOOST_VERSION}")
set(boost_log_setup_DIR "${Boost_LIB_DIR}/cmake/boost_log_setup-${BOOST_VERSION}")
set(boost_program_options_DIR "${Boost_LIB_DIR}/cmake/boost_program_options-${BOOST_VERSION}")
set(boost_random_DIR "${Boost_LIB_DIR}/cmake/boost_random-${BOOST_VERSION}")
set(boost_regex_DIR "${Boost_LIB_DIR}/cmake/boost_regex-${BOOST_VERSION}")
set(boost_system_DIR "${Boost_LIB_DIR}/cmake/boost_system-${BOOST_VERSION}")
set(boost_thread_DIR "${Boost_LIB_DIR}/cmake/boost_thread-${BOOST_VERSION}")
set(boost_context_DIR "${Boost_LIB_DIR}/cmake/boost_context-${BOOST_VERSION}")
set(boost_coroutine_DIR "${Boost_LIB_DIR}/cmake/boost_coroutine-${BOOST_VERSION}")
set(boost_unit_test_framework_DIR "${Boost_LIB_DIR}/cmake/boost_unit_test_framework-${BOOST_VERSION}")
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_LIBS ON)
set(Boost_NO_SYSTEM_PATHS ON)
option(Boost_USE_STATIC_RUNTIME "Use static runtimes" ON)
set(_BOOST_CACHE_ARGS
    -DBOOST_ROOT:PATH=${_BOOST_ROOT}
    -DBoost_DIR:PATH=${Boost_DIR}/Boost-${BOOST_VERSION}
    -DBoost_INCLUDE_DIR:PATH=${Boost_INCLUDE_DIR}
    -Dboost_headers_DIR:PATH=${Boost_DIR}/boost_headers-${BOOST_VERSION}
    -Dboost_date_time_DIR:PATH=${Boost_DIR}/boost_date_time-${BOOST_VERSION}
    -Dboost_filesystem_DIR:PATH=${Boost_DIR}/boost_filesystem-${BOOST_VERSION}
    -Dboost_program_options_DIR:PATH=${Boost_DIR}/boost_program_options-${BOOST_VERSION}
    -Dboost_random_DIR:PATH=${Boost_DIR}/boost_random-${BOOST_VERSION}
    -Dboost_regex_DIR:PATH=${Boost_DIR}/boost_regex-${BOOST_VERSION}
    -Dboost_system_DIR:PATH=${Boost_DIR}/boost_system-${BOOST_VERSION}
    -Dboost_context_DIR:PATH=${Boost_DIR}/boost_context-${BOOST_VERSION}
    -Dboost_coroutine_DIR:PATH=${Boost_DIR}/boost_coroutine-${BOOST_VERSION}
    -Dboost_thread_DIR:PATH=${Boost_DIR}/boost_thread-${BOOST_VERSION}
    -Dboost_log_DIR:PATH=${Boost_DIR}/boost_log-${BOOST_VERSION}
    -Dboost_log_setup_DIR:PATH=${Boost_DIR}/boost_log_setup-${BOOST_VERSION}
    -Dboost_unit_test_framework_DIR:PATH=${Boost_DIR}/boost_unit_test_framework-${BOOST_VERSION}
    -Dboost_json_DIR:PATH=${Boost_DIR}/boost_json-${BOOST_VERSION}
    -DBoost_USE_STATIC_RUNTIME:BOOL=ON
    -DBoost_NO_SYSTEM_PATHS:BOOL=ON
    -DBoost_USE_MULTITHREADED:BOOL=ON
    -DBoost_USE_STATIC_LIBS:BOOL=ON
    -DBoost_USE_STATIC_RUNTIME:BOOL=ON
)

option(SGNS_STACKTRACE_BACKTRACE "Use BOOST_STACKTRACE_USE_BACKTRACE in stacktraces, for POSIX" OFF)

if(SGNS_STACKTRACE_BACKTRACE)
    add_definitions(-DSGNS_STACKTRACE_BACKTRACE=1)

    if(BACKTRACE_INCLUDE)
        add_definitions(-DBOOST_STACKTRACE_BACKTRACE_INCLUDE_FILE=${BACKTRACE_INCLUDE})
    endif()
endif()

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
endif()
find_package(Boost REQUIRED COMPONENTS container date_time filesystem random regex system thread log log_setup program_options unit_test_framework json context coroutine)
include_directories(SYSTEM ${Boost_INCLUDE_DIRS})

# SQLiteModernCpp project
set(SQLiteModernCpp_ROOT_DIR "${_THIRDPARTY_BUILD_DIR}/SQLiteModernCpp")
set(SQLiteModernCpp_DIR "${SQLiteModernCpp_ROOT_DIR}/lib/cmake/SQLiteModernCpp")
set(SQLiteModernCpp_LIB_DIR "${SQLiteModernCpp_ROOT_DIR}/lib")
set(SQLiteModernCpp_INCLUDE_DIR "${SQLiteModernCpp_ROOT_DIR}/include")

# SQLiteModernCpp project
set(sqlite3_ROOT_DIR "${_THIRDPARTY_BUILD_DIR}/sqlite3")
set(sqlite3_DIR "${sqlite3_ROOT_DIR}/lib/cmake/sqlite3")
set(sqlite3_LIB_DIR "${sqlite3_ROOT_DIR}/lib")
set(sqlite3_INCLUDE_DIR "${sqlite3_ROOT_DIR}/include")

# cares
set(c-ares_DIR "${_THIRDPARTY_BUILD_DIR}/cares/lib/cmake/c-ares" CACHE PATH "Path to c-ares install folder")
set(c-ares_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/cares/include" CACHE PATH "Path to c-ares include folder")

# libp2p
set(libp2p_DIR "${_THIRDPARTY_BUILD_DIR}/libp2p/lib/cmake/libp2p")
find_package(libp2p CONFIG REQUIRED)

# Find and include cares if libp2p have not included it
if(NOT TARGET c-ares::cares_static)
    find_package(c-ares CONFIG REQUIRED)
endif()

include_directories(${c-ares_INCLUDE_DIR})

# VulkanHeaders
set(VulkanHeaders_DIR "${_THIRDPARTY_BUILD_DIR}/Vulkan-Headers/share/cmake/VulkanHeaders" CACHE PATH "Path to Vulkan-Headers install folder")
find_package(VulkanHeaders CONFIG REQUIRED)
# Vulkan
find_package(Vulkan)

if(NOT TARGET Vulkan::Vulkan)
    set(Vulkan_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/Vulkan-Headers/include")
    if(NOT DEFINED $ENV{VULKAN_SDK})
        set(ENV{VULKAN_SDK} "${_THIRDPARTY_BUILD_DIR}/Vulkan-Loader")
    endif()

    find_package(Vulkan REQUIRED)
endif()

# Override Vulkan::Vulkan to use our vendored Vulkan-Headers on all platforms.
# vk-bootstrap was built against our headers (v1.4); mixing with system/NDK
# headers (v1.3 or other versions) causes unknown-type errors in
# VkBootstrapDispatch.h and VkBootstrapFeatureChain.h.
set_target_properties(Vulkan::Vulkan PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_THIRDPARTY_BUILD_DIR}/Vulkan-Headers/include"
)

# On macOS, libMoltenVK.a contains Objective-C code that calls Metal.
# The ObjC runtime (-lobjc) and Metal/AppKit frameworks must be linked by
# every consumer of Vulkan::Vulkan or the linker fails with undefined
# _objc_msgSend / _objc_retain / _objc_release etc.
if(APPLE)
    target_link_libraries(Vulkan::Vulkan INTERFACE
        "-framework Metal"
        "-framework IOSurface"
        "-framework QuartzCore"
        "-framework Foundation"
        "-framework CoreFoundation"
        "-framework CoreGraphics"
        "-framework IOKit"
        "-framework AppKit"
    )
endif()

# vk-bootstrap
set(vk-bootstrap_DIR "${_THIRDPARTY_BUILD_DIR}/vk-bootstrap/lib/cmake/vk-bootstrap")
find_package(vk-bootstrap CONFIG REQUIRED)

# SPIRV-Tools — no longer a standalone build.  libshaderc_combined (linked via
# shaderc::shaderc below) statically bundles the exact same SPIRV-Tools code at the
# exact same pinned commit (v2024.3 DEPS).  The spirv-tools include path is folded into
# shaderc::shaderc's INTERFACE_INCLUDE_DIRECTORIES so <spirv-tools/libspirv.hpp> resolves.

# shaderc — installs no CMake package config (confirmed in 02-02-RESEARCH.md against
# github.com/google/shaderc/issues/1369 and github.com/microsoft/vcpkg/issues/23208); hand-written
# IMPORTED target required, mirroring thirdparty/build/CommonTargets.cmake's own target.
# libshaderc_combined statically bundles glslang+SPIRV-Tools and installs spirv-tools
# headers into <prefix>/include/spirv-tools/, so <spirv-tools/libspirv.hpp> resolves
# for consumers that call spvtools::SpirvTools::Validate() directly (SHADER-02).
if(NOT TARGET shaderc::shaderc)
    add_library(shaderc::shaderc STATIC IMPORTED GLOBAL)
    set_target_properties(shaderc::shaderc PROPERTIES
        IMPORTED_LOCATION "${_THIRDPARTY_BUILD_DIR}/shaderc/lib/${CMAKE_STATIC_LIBRARY_PREFIX}shaderc_combined${CMAKE_STATIC_LIBRARY_SUFFIX}"
        INTERFACE_INCLUDE_DIRECTORIES "${_THIRDPARTY_BUILD_DIR}/shaderc/include"
    )
endif()

# ipfs-lite-cpp
set(ipfs-lite-cpp_DIR "${_THIRDPARTY_BUILD_DIR}/ipfs-lite-cpp/lib/cmake/ipfs-lite-cpp")
find_package(ipfs-lite-cpp CONFIG REQUIRED)

# ipfs-pubsub
set(ipfs-pubsub_DIR "${_THIRDPARTY_BUILD_DIR}/ipfs-pubsub/lib/cmake/ipfs-pubsub")
find_package(ipfs-pubsub CONFIG REQUIRED)

# ipfs-bitswap-cpp
set(ipfs-bitswap-cpp_DIR "${_THIRDPARTY_BUILD_DIR}/ipfs-bitswap-cpp/lib/cmake/ipfs-bitswap-cpp")
find_package(ipfs-bitswap-cpp CONFIG REQUIRED)

# ed25519
set(ed25519_DIR "${_THIRDPARTY_BUILD_DIR}/ed25519/lib/cmake/ed25519")
find_package(ed25519 CONFIG REQUIRED)

set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON CACHE BOOL "Suppress developer warnings" FORCE)
# Globally suppress ALL CMake deprecation warnings (including from third-party Config.cmake files)
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "Disable deprecation warnings" FORCE)
# RapidJSON
set(RapidJSON_DIR "${_THIRDPARTY_BUILD_DIR}/rapidjson/lib/cmake/RapidJSON")
find_package(RapidJSON CONFIG REQUIRED)

# secp256k1
set(libsecp256k1_DIR "${_THIRDPARTY_BUILD_DIR}/libsecp256k1/lib/cmake/libsecp256k1")
find_package(libsecp256k1 CONFIG REQUIRED)

# xxHash
set(xxHash_DIR "${_THIRDPARTY_BUILD_DIR}/xxhash/lib/cmake/xxHash")
find_package(xxHash CONFIG REQUIRED)

# zlib
set(ZLIB_ROOT "${_THIRDPARTY_BUILD_DIR}/zlib")

# Prefer package config files while loading Libssh2's dependencies.
# Libssh2 config calls `find_dependency(ZLIB)` without `CONFIG`, which can
# otherwise resolve to CMake's FindZLIB module on Windows CI.
set(_SGNS_CMAKE_FIND_PACKAGE_PREFER_CONFIG_WAS_DEFINED FALSE)
if(DEFINED CMAKE_FIND_PACKAGE_PREFER_CONFIG)
    set(_SGNS_CMAKE_FIND_PACKAGE_PREFER_CONFIG_WAS_DEFINED TRUE)
    set(_SGNS_CMAKE_FIND_PACKAGE_PREFER_CONFIG_PREV "${CMAKE_FIND_PACKAGE_PREFER_CONFIG}")
endif()
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)

# libssh2
set(Libssh2_DIR "${_THIRDPARTY_BUILD_DIR}/libssh2/lib/cmake/libssh2")
find_package(Libssh2 CONFIG REQUIRED)

if(_SGNS_CMAKE_FIND_PACKAGE_PREFER_CONFIG_WAS_DEFINED)
    set(CMAKE_FIND_PACKAGE_PREFER_CONFIG "${_SGNS_CMAKE_FIND_PACKAGE_PREFER_CONFIG_PREV}")
else()
    unset(CMAKE_FIND_PACKAGE_PREFER_CONFIG)
endif()
unset(_SGNS_CMAKE_FIND_PACKAGE_PREFER_CONFIG_PREV)
unset(_SGNS_CMAKE_FIND_PACKAGE_PREFER_CONFIG_WAS_DEFINED)

# AsyncIOManager
set(AsyncIOManager_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/AsyncIOManager/include")
set(AsyncIOManager_DIR "${_THIRDPARTY_BUILD_DIR}/AsyncIOManager/lib/cmake/AsyncIOManager")
find_package(AsyncIOManager CONFIG REQUIRED)

# --------------------------------------------------------
# Set config of crypto3
add_library(crypto3::algebra INTERFACE IMPORTED)
add_library(crypto3::block INTERFACE IMPORTED)
add_library(crypto3::blueprint INTERFACE IMPORTED)
add_library(crypto3::codec INTERFACE IMPORTED)
add_library(crypto3::math INTERFACE IMPORTED)
add_library(crypto3::multiprecision INTERFACE IMPORTED)
add_library(crypto3::pkpad INTERFACE IMPORTED)
add_library(crypto3::pubkey INTERFACE IMPORTED)
add_library(crypto3::random INTERFACE IMPORTED)
add_library(crypto3::zk INTERFACE IMPORTED)
add_library(marshalling::core INTERFACE IMPORTED)
add_library(marshalling::crypto3_algebra INTERFACE IMPORTED)
add_library(marshalling::crypto3_multiprecision INTERFACE IMPORTED)
add_library(marshalling::crypto3_zk INTERFACE IMPORTED)

set_target_properties(crypto3::algebra PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ZKLLVM_BUILD_DIR}/zkLLVM/include"
)
set_target_properties(crypto3::block PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ZKLLVM_BUILD_DIR}/zkLLVM/include"
)
set_target_properties(crypto3::blueprint PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ZKLLVM_BUILD_DIR}/zkLLVM/include"
)
set_target_properties(crypto3::codec PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ZKLLVM_BUILD_DIR}/zkLLVM/include"
)
set_target_properties(crypto3::math PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ZKLLVM_BUILD_DIR}/zkLLVM/include"
)
set_target_properties(crypto3::multiprecision PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ZKLLVM_BUILD_DIR}/zkLLVM/include"
)
set_target_properties(crypto3::pkpad PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ZKLLVM_BUILD_DIR}/zkLLVM/include"
)
set_target_properties(crypto3::pubkey PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ZKLLVM_BUILD_DIR}/zkLLVM/include"
)
set_target_properties(crypto3::random PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ZKLLVM_BUILD_DIR}/zkLLVM/include"
)
set_target_properties(crypto3::zk PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ZKLLVM_BUILD_DIR}/zkLLVM/include"
)
set_target_properties(marshalling::core PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ZKLLVM_BUILD_DIR}/zkLLVM/include"
)
set_target_properties(marshalling::crypto3_algebra PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ZKLLVM_BUILD_DIR}/zkLLVM/include"
)
set_target_properties(marshalling::crypto3_multiprecision PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ZKLLVM_BUILD_DIR}/zkLLVM/include"
)
set_target_properties(marshalling::crypto3_zk PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ZKLLVM_BUILD_DIR}/zkLLVM/include"
)

# zkLLVM
set(zkLLVM_INCLUDE_DIR "${ZKLLVM_BUILD_DIR}/zkLLVM/include")
include_directories(SYSTEM ${zkLLVM_INCLUDE_DIR})


# circifier
set(LLVM_DIR "${ZKLLVM_BUILD_DIR}/zkLLVM/lib/cmake/llvm")
find_package(LLVM CONFIG REQUIRED)


# gnus_upnp
set(gnus_upnp_DIR "${_THIRDPARTY_BUILD_DIR}/gnus_upnp/lib/cmake/gnus_upnp")
find_package(gnus_upnp CONFIG REQUIRED)

#json.hpp
set(nlohmann_json_DIR "${_THIRDPARTY_BUILD_DIR}/json/share/cmake/nlohmann_json")
find_package(nlohmann_json CONFIG REQUIRED)

# wallet-core
set(TrustWalletCore_LIBRARY_DIR "${_THIRDPARTY_BUILD_DIR}/wallet-core/lib")
set(TrustWalletCore_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/wallet-core/include")

find_library(TrezorCrypto_PATH TrezorCrypto PATHS ${TrustWalletCore_LIBRARY_DIR} REQUIRED)
find_library(wallet_core_rs_PATH wallet_core_rs PATHS ${TrustWalletCore_LIBRARY_DIR} REQUIRED)
find_library(TrustWalletCore_PATH TrustWalletCore PATHS ${TrustWalletCore_LIBRARY_DIR} REQUIRED)

add_library(TrezorCrypto STATIC IMPORTED)
add_library(wallet_core_rs STATIC IMPORTED)
add_library(TrustWalletCore STATIC IMPORTED)

set_target_properties(TrezorCrypto PROPERTIES IMPORTED_LOCATION "${TrezorCrypto_PATH}")
set_target_properties(wallet_core_rs PROPERTIES IMPORTED_LOCATION "${wallet_core_rs_PATH}")
set_target_properties(TrustWalletCore PROPERTIES IMPORTED_LOCATION "${TrustWalletCore_PATH}")

target_include_directories(TrustWalletCore INTERFACE "${TrustWalletCore_INCLUDE_DIR}")

if (APPLE)
    find_library(CORE_FOUNDATION CoreFoundation)
    find_library(SECURITY_FRAMEWORK Security)
endif()

if(${IOS})
    target_link_libraries(TrustWalletCore INTERFACE ${SECURITY_FRAMEWORK})
endif()

include_directories(
    ${PROJECT_ROOT}/src
)

include_directories(
        ${PROJECT_ROOT}/ProofSystem/include
)

include_directories(
        ${PROJECT_ROOT}/SGProcessingManager/include
)

include_directories(
        ${PROJECT_ROOT}/evmrelay/include
)

include_directories(
    ${PROJECT_ROOT}/app
)

ADD_DEFINITIONS(-D_HAS_AUTO_PTR_ETC=1)

print("CMAKE_HOST_SYSTEM_NAME: ${CMAKE_HOST_SYSTEM_NAME}")
print("CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")
print("CMAKE_CXX_STANDARD: ${CMAKE_CXX_STANDARD}")
print("CMAKE_CXX_STANDARD_REQUIRED: ${CMAKE_CXX_STANDARD_REQUIRED}")
print("C flags: ${CMAKE_C_FLAGS}")
print("CXX flags: ${CMAKE_CXX_FLAGS}")
print("C Debug flags: ${CMAKE_C_FLAGS_DEBUG}")
print("CXX Debug flags: ${CMAKE_CXX_FLAGS_DEBUG}")
print("C Release flags: ${CMAKE_C_FLAGS_RELEASE}")
print("CXX Release flags: ${CMAKE_CXX_FLAGS_RELEASE}")

link_directories(
    ${Boost_LIB_DIR}
    ${ipfs-lite-cpp_LIB_DIR}
)

# enable_testing() must run before any add_subdirectory() below so that CTest's
# per-directory CTestTestfile.cmake chain (root -> SGProcessingManager -> test ->
# capability/artifacts/capture) is actually generated; calling it only inside the
# later if(BUILD_TESTING) block (after these subdirectories are already configured)
# left ctest silently unable to discover any test registered under them, even
# though the leaf CMakeLists.txt files call enable_testing()/add_test() themselves.
if(BUILD_TESTING)
    enable_testing()
endif()

add_subdirectory(${PROJECT_ROOT}/ProofSystem ${CMAKE_BINARY_DIR}/ProofSystem)
add_subdirectory(${PROJECT_ROOT}/SGProcessingManager ${CMAKE_BINARY_DIR}/SGProcessingManager)
add_subdirectory(${PROJECT_ROOT}/evmrelay ${CMAKE_BINARY_DIR}/evmrelay)
add_subdirectory(${PROJECT_ROOT}/src ${CMAKE_BINARY_DIR}/src)

#add_subdirectory(${PROJECT_ROOT}/GeniusKDF ${CMAKE_BINARY_DIR}/GeniusKDF)

if(BUILD_TESTING)
    add_subdirectory(${PROJECT_ROOT}/test ${CMAKE_BINARY_DIR}/test)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(${PROJECT_ROOT}/example ${CMAKE_BINARY_DIR}/example)
endif()

install(
    EXPORT supergeniusTargets
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SuperGenius
    NAMESPACE sgns::
)

# generate the config file that is includes the exports
configure_package_config_file(${PROJECT_ROOT}/cmake/config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/SuperGeniusConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SuperGenius
    NO_SET_AND_CHECK_MACRO
    NO_CHECK_REQUIRED_COMPONENTS_MACRO
)

# generate the version file for the config file
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/SuperGeniusConfigVersion.cmake"
    VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}"
    COMPATIBILITY AnyNewerVersion
)

# install header files
#install_hfile(${PROJECT_ROOT}/src/api)
#install_hfile(${PROJECT_ROOT}/src/authorship)
#install_hfile(${PROJECT_ROOT}/src/application)
#install_hfile(${PROJECT_ROOT}/src/base)
#install_hfile(${PROJECT_ROOT}/src/blockchain)
#install_hfile(${PROJECT_ROOT}/src/clock)
#install_hfile(${PROJECT_ROOT}/src/crdt)
#install_hfile(${PROJECT_ROOT}/src/crypto)
#install_hfile(${PROJECT_ROOT}/src/extensions)
#install_hfile(${PROJECT_ROOT}/src/injector)
#install_hfile(${PROJECT_ROOT}/src/macro)
#install_hfile(${PROJECT_ROOT}/src/network)
#install_hfile(${PROJECT_ROOT}/src/outcome)
#install_hfile(${PROJECT_ROOT}/src/processing)
#install_hfile(${PROJECT_ROOT}/src/primitives)
#install_hfile(${PROJECT_ROOT}/src/runtime)
#install_hfile(${PROJECT_ROOT}/src/scale)
#install_hfile(${PROJECT_ROOT}/src/storage)
#install_hfile(${PROJECT_ROOT}/src/subscription)
#install_hfile(${PROJECT_ROOT}/src/transaction_pool)
#install_hfile(${PROJECT_ROOT}/src/verification)
#install_hfile(${PROJECT_ROOT}/src/account)
#install_hfile(${PROJECT_ROOT}/app/integration)
#install_hfile(${PROJECT_ROOT}/src/local_secure_storage)
#install_hfile(${PROJECT_ROOT}/src/singleton)
#install_hfile(${PROJECT_ROOT}/src/coinprices)
#install_hfile(${PROJECT_ROOT}/ProcessingSchema/generated)
#
## install proto header files
#install_hfile(${CMAKE_CURRENT_BINARY_DIR}/generated/crdt)
#install_hfile(${CMAKE_CURRENT_BINARY_DIR}/generated/processing)
#install_hfile(${CMAKE_CURRENT_BINARY_DIR}/generated/account)
#install_hfile(${CMAKE_CURRENT_BINARY_DIR}/generated/blockchain)
#install_hfile(${CMAKE_CURRENT_BINARY_DIR}/generated/proof)

# install the configuration file
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/SuperGeniusConfig.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SuperGenius
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/SuperGeniusConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SuperGenius
)
