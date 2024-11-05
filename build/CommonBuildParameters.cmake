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

if(BUILD_TESTING)
    set(GTest_DIR "${_THIRDPARTY_BUILD_DIR}/GTest/lib/cmake/GTest")
    set(GTest_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/GTest/include")
    find_package(GTest CONFIG REQUIRED)
    include_directories(${GTest_INCLUDE_DIR})
endif()

# absl
if(NOT DEFINED absl_DIR)
    set(absl_DIR "${_THIRDPARTY_BUILD_DIR}/grpc/lib/cmake/absl")
endif()

# utf8_range
if(NOT DEFINED utf8_range_DIR)
    set(utf8_range_DIR "${_THIRDPARTY_BUILD_DIR}/grpc/lib/cmake/utf8_range")
endif()

# protobuf project
if(NOT DEFINED Protobuf_DIR)
    set(Protobuf_DIR "${_THIRDPARTY_BUILD_DIR}/grpc/lib/cmake/protobuf")
endif()

if(NOT DEFINED grpc_INCLUDE_DIR)
    set(grpc_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/grpc/include")
endif()

if(NOT DEFINED Protobuf_INCLUDE_DIR)
    set(Protobuf_INCLUDE_DIR "${grpc_INCLUDE_DIR}/google/protobuf")
endif()

find_package(Protobuf CONFIG REQUIRED)

if(NOT DEFINED PROTOC_EXECUTABLE)
    set(PROTOC_EXECUTABLE "${_THIRDPARTY_BUILD_DIR}/grpc/bin/protoc${CMAKE_EXECUTABLE_SUFFIX}")
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

#mnn
set(MNN_DIR "${_THIRDPARTY_BUILD_DIR}/MNN/lib/cmake/MNN")
find_package(MNN CONFIG REQUIRED)
set(MNN_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/MNN/include")
message(STATIS "INCLUDE DIR ${MNN_INCLUDE_DIR}")
include_directories(${MNN_INCLUDE_DIR})
get_target_property(MNN_LIB_PATH MNN::MNN IMPORTED_LOCATION)
message(STATUS "MNN PATH ${MNN_LIB_PATH}")
# openssl project
set(OPENSSL_DIR "${_THIRDPARTY_BUILD_DIR}/openssl/build/${CMAKE_SYSTEM_NAME}${ABI_SUBFOLDER_NAME}" CACHE PATH "Path to OpenSSL install folder")
set(OPENSSL_USE_STATIC_LIBS ON CACHE BOOL "OpenSSL use static libs")
set(OPENSSL_MSVC_STATIC_RT ON CACHE BOOL "OpenSSL use static RT")
set(OPENSSL_ROOT_DIR "${OPENSSL_DIR}" CACHE PATH "Path to OpenSSL install root folder")
set(OPENSSL_INCLUDE_DIR "${OPENSSL_DIR}/include" CACHE PATH "Path to OpenSSL include folder")
set(OPENSSL_LIBRARIES "${OPENSSL_DIR}/lib" CACHE PATH "Path to OpenSSL lib folder")
set(OPENSSL_CRYPTO_LIBRARY ${OPENSSL_LIBRARIES}/libcrypto${CMAKE_STATIC_LIBRARY_SUFFIX} CACHE PATH "Path to OpenSSL crypto lib")
set(OPENSSL_SSL_LIBRARY ${OPENSSL_LIBRARIES}/libssl${CMAKE_STATIC_LIBRARY_SUFFIX} CACHE PATH "Path to OpenSSL ssl lib")
find_package(OpenSSL REQUIRED)

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
set(_BOOST_ROOT "${_THIRDPARTY_BUILD_DIR}/boost/build/${CMAKE_SYSTEM_NAME}${ABI_SUBFOLDER_NAME}")
set(Boost_LIB_DIR "${_BOOST_ROOT}/lib")
set(Boost_INCLUDE_DIR "${_BOOST_ROOT}/include/boost-${BOOST_VERSION_2U}")
set(Boost_DIR "${Boost_LIB_DIR}/cmake/Boost-${BOOST_VERSION}")
set(boost_headers_DIR "${Boost_LIB_DIR}/cmake/boost_headers-${BOOST_VERSION}")
set(boost_random_DIR "${Boost_LIB_DIR}/cmake/boost_random-${BOOST_VERSION}")
set(boost_system_DIR "${Boost_LIB_DIR}/cmake/boost_system-${BOOST_VERSION}")
set(boost_filesystem_DIR "${Boost_LIB_DIR}/cmake/boost_filesystem-${BOOST_VERSION}")
set(boost_program_options_DIR "${Boost_LIB_DIR}/cmake/boost_program_options-${BOOST_VERSION}")
set(boost_date_time_DIR "${Boost_LIB_DIR}/cmake/boost_date_time-${BOOST_VERSION}")
set(boost_regex_DIR "${Boost_LIB_DIR}/cmake/boost_regex-${BOOST_VERSION}")
set(boost_atomic_DIR "${Boost_LIB_DIR}/cmake/boost_atomic-${BOOST_VERSION}")
set(boost_chrono_DIR "${Boost_LIB_DIR}/cmake/boost_chrono-${BOOST_VERSION}")
set(boost_log_DIR "${Boost_LIB_DIR}/cmake/boost_log-${BOOST_VERSION}")
set(boost_log_setup_DIR "${Boost_LIB_DIR}/cmake/boost_log_setup-${BOOST_VERSION}")
set(boost_thread_DIR "${Boost_LIB_DIR}/cmake/boost_thread-${BOOST_VERSION}")
set(boost_unit_test_framework_DIR "${Boost_LIB_DIR}/cmake/boost_unit_test_framework-${BOOST_VERSION}")
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_LIBS ON)
set(Boost_NO_SYSTEM_PATHS ON)
option(Boost_USE_STATIC_RUNTIME "Use static runtimes" ON)

option(SGNS_STACKTRACE_BACKTRACE "Use BOOST_STACKTRACE_USE_BACKTRACE in stacktraces, for POSIX" OFF)

if(SGNS_STACKTRACE_BACKTRACE)
    add_definitions(-DSGNS_STACKTRACE_BACKTRACE=1)

    if(BACKTRACE_INCLUDE)
        add_definitions(-DBOOST_STACKTRACE_BACKTRACE_INCLUDE_FILE=${BACKTRACE_INCLUDE})
    endif()
endif()

# header only libraries must not be added here
find_package(Boost REQUIRED COMPONENTS date_time filesystem random regex system thread log log_setup program_options unit_test_framework)
include_directories(${Boost_INCLUDE_DIRS})

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

# sr25519-donna
set(sr25519-donna_DIR "${_THIRDPARTY_BUILD_DIR}/sr25519-donna/lib/cmake/sr25519-donna")
find_package(sr25519-donna CONFIG REQUIRED)

# RapidJSON
set(RapidJSON_DIR "${_THIRDPARTY_BUILD_DIR}/rapidjson/lib/cmake/RapidJSON")
find_package(RapidJSON CONFIG REQUIRED)

# binaryen
# set(binaryen_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/binaryen/include")
# set(binaryen_LIBRARIES "${_THIRDPARTY_BUILD_DIR}/binaryen/lib")
# set(binaryen_DIR "${_THIRDPARTY_BUILD_DIR}/binaryen/lib/cmake/binaryen")
# find_package(binaryen CONFIG REQUIRED)
# include_directories(${binaryen_INCLUDE_DIR} ${binaryen_INCLUDE_DIR}/binaryen)

# secp256k1
set(libsecp256k1_DIR "${_THIRDPARTY_BUILD_DIR}/libsecp256k1/lib/cmake/libsecp256k1")
find_package(libsecp256k1 CONFIG REQUIRED)

# xxhash
set(xxhash_DIR "${_THIRDPARTY_BUILD_DIR}/xxhash/lib/cmake/xxhash")
find_package(xxhash CONFIG REQUIRED)

# libssh2
set(Libssh2_DIR "${_THIRDPARTY_BUILD_DIR}/libssh2/lib/cmake/libssh2")
find_package(Libssh2 CONFIG REQUIRED)

# AsyncIOManager
set(AsyncIOManager_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/AsyncIOManager/include")
set(AsyncIOManager_DIR "${_THIRDPARTY_BUILD_DIR}/AsyncIOManager/lib/cmake/AsyncIOManager")
find_package(AsyncIOManager CONFIG REQUIRED)

# --------------------------------------------------------
# Set config of crypto3
# set(crypto3_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/crypto3/include")
# set(crypto3_LIBRARY_DIR "${_THIRDPARTY_BUILD_DIR}/crypto3/lib")
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
    INTERFACE_INCLUDE_DIRECTORIES "${_THIRDPARTY_BUILD_DIR}/zkLLVM/include"
)
set_target_properties(crypto3::block PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_THIRDPARTY_BUILD_DIR}/zkLLVM/include"
)
set_target_properties(crypto3::blueprint PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_THIRDPARTY_BUILD_DIR}/zkLLVM/include"
)
set_target_properties(crypto3::codec PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_THIRDPARTY_BUILD_DIR}/zkLLVM/include"
)
set_target_properties(crypto3::math PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_THIRDPARTY_BUILD_DIR}/zkLLVM/include"
)
set_target_properties(crypto3::multiprecision PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_THIRDPARTY_BUILD_DIR}/zkLLVM/include"
)
set_target_properties(crypto3::pkpad PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_THIRDPARTY_BUILD_DIR}/zkLLVM/include"
)
set_target_properties(crypto3::pubkey PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_THIRDPARTY_BUILD_DIR}/zkLLVM/include"
)
set_target_properties(crypto3::random PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_THIRDPARTY_BUILD_DIR}/zkLLVM/include"
)
set_target_properties(crypto3::zk PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_THIRDPARTY_BUILD_DIR}/zkLLVM/include"
)
set_target_properties(marshalling::core PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_THIRDPARTY_BUILD_DIR}/zkLLVM/include"
)
set_target_properties(marshalling::crypto3_algebra PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_THIRDPARTY_BUILD_DIR}/zkLLVM/include"
)
set_target_properties(marshalling::crypto3_multiprecision PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_THIRDPARTY_BUILD_DIR}/zkLLVM/include"
)
set_target_properties(marshalling::crypto3_zk PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_THIRDPARTY_BUILD_DIR}/zkLLVM/include"
)

# set(crypto3_hash_DIR "${_THIRDPARTY_BUILD_DIR}/zkLLVM/lib/cmake/crypto3_hash")
# set(crypto3_algebra_DIR "${_THIRDPARTY_BUILD_DIR}/zkLLVM/lib/cmake/crypto3_algebra")
# find_package(crypto3_algebra CONFIG REQUIRED)
# find_package(crypto3_hash CONFIG REQUIRED)
# include_directories(${crypto3_INCLUDE_DIR})
include_directories(
    "${THIRDPARTY_DIR}/zkLLVM/libs/crypto3/libs/algebra/include"
    "${THIRDPARTY_DIR}/zkLLVM/libs/crypto3/libs/block/include"
    "${THIRDPARTY_DIR}/zkLLVM/libs/crypto3/libs/codec/include"
    "${THIRDPARTY_DIR}/zkLLVM/libs/crypto3/libs/containers/include"
    "${THIRDPARTY_DIR}/zkLLVM/libs/crypto3/libs/hash/include"
    "${THIRDPARTY_DIR}/zkLLVM/libs/crypto3/libs/kdf/include"
    "${THIRDPARTY_DIR}/zkLLVM/libs/crypto3/libs/mac/include"
    "${THIRDPARTY_DIR}/zkLLVM/libs/crypto3/libs/math/include"
    "${THIRDPARTY_DIR}/zkLLVM/libs/crypto3/libs/marshalling/algebra/include"
    "${THIRDPARTY_DIR}/zkLLVM/libs/crypto3/libs/marshalling/core/include"
    "${THIRDPARTY_DIR}/zkLLVM/libs/crypto3/libs/marshalling/multiprecision/include"
    "${THIRDPARTY_DIR}/zkLLVM/libs/crypto3/libs/marshalling/zk/include"
    "${THIRDPARTY_DIR}/zkLLVM/libs/crypto3/libs/modes/include"
    "${THIRDPARTY_DIR}/zkLLVM/libs/crypto3/libs/multiprecision/include"
    "${THIRDPARTY_DIR}/zkLLVM/libs/crypto3/libs/passhash/include"
    "${THIRDPARTY_DIR}/zkLLVM/libs/crypto3/libs/pbkdf/include"
    "${THIRDPARTY_DIR}/zkLLVM/libs/crypto3/libs/pkpad/include"
    "${THIRDPARTY_DIR}/zkLLVM/libs/crypto3/libs/pubkey/include"
    "${THIRDPARTY_DIR}/zkLLVM/libs/crypto3/libs/random/include"
    "${THIRDPARTY_DIR}/zkLLVM/libs/crypto3/libs/stream/include"
    "${THIRDPARTY_DIR}/zkLLVM/libs/crypto3/libs/threshold/include"
    "${THIRDPARTY_DIR}/zkLLVM/libs/crypto3/libs/vdf/include"
    "${THIRDPARTY_DIR}/zkLLVM/libs/crypto3/libs/zk/include"

)

# blueprint
set(blueprint_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/zkLLVM/include")

# set(blueprint_LIBRARY_DIR "${_THIRDPARTY_BUILD_DIR}/blueprint/lib")
# set(blueprint_DIR "${_THIRDPARTY_BUILD_DIR}/blueprint/lib/cmake/blueprint")
# find_package(blueprint CONFIG REQUIRED)
include_directories(${blueprint_INCLUDE_DIR})

# circifier
# set(LLVM_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/circifier/include")
# set(LLVM_LIBRARY_DIR "${_THIRDPARTY_BUILD_DIR}/circifier/lib")
set(LLVM_DIR "${_THIRDPARTY_BUILD_DIR}/zkLLVM/lib/cmake/llvm")
find_package(LLVM CONFIG REQUIRED)

# include_directories(${LLVM_INCLUDE_DIR})

# assigner
# set(assigner_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/assigner/include")
# set(assigner_LIBRARY_DIR "${_THIRDPARTY_BUILD_DIR}/assigner/lib")
# set(assigner_DIR "${_THIRDPARTY_BUILD_DIR}/assigner/lib/cmake/assigner")
# find_package(assigner CONFIG REQUIRED)
# include_directories(${assigner_INCLUDE_DIR})

# gnus_upnp
set(gnus_upnp_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/gnus_upnp/include")
set(gnus_upnp_LIBRARY_DIR "${_THIRDPARTY_BUILD_DIR}/gnus_upnp/lib")
set(gnus_upnp_DIR "${_THIRDPARTY_BUILD_DIR}/gnus_upnp/lib/cmake/gnus_upnp")
find_package(gnus_upnp CONFIG REQUIRED)
include_directories(${gnus_upnp_INCLUDE_DIR})

include_directories(
    ${PROJECT_ROOT}/src
)
include_directories(
    ${PROJECT_ROOT}/GeniusKDF
)
include_directories(
    ${PROJECT_ROOT}/ProofSystem
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

add_subdirectory(${PROJECT_ROOT}/src ${CMAKE_BINARY_DIR}/src)

add_subdirectory(${PROJECT_ROOT}/GeniusKDF ${CMAKE_BINARY_DIR}/GeniusKDF)

add_subdirectory(${PROJECT_ROOT}/ProofSystem ${CMAKE_BINARY_DIR}/ProofSystem)

# add_subdirectory(${PROJECT_ROOT}/app ${CMAKE_BINARY_DIR}/app)
if(BUILD_TESTING)
    enable_testing()
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
install_hfile(${PROJECT_ROOT}/src/api)
install_hfile(${PROJECT_ROOT}/src/authorship)
install_hfile(${PROJECT_ROOT}/src/application)
install_hfile(${PROJECT_ROOT}/src/base)
install_hfile(${PROJECT_ROOT}/src/blockchain)
install_hfile(${PROJECT_ROOT}/src/clock)
install_hfile(${PROJECT_ROOT}/src/crdt)
install_hfile(${PROJECT_ROOT}/src/crypto)
install_hfile(${PROJECT_ROOT}/src/extensions)
install_hfile(${PROJECT_ROOT}/src/injector)
install_hfile(${PROJECT_ROOT}/src/macro)
install_hfile(${PROJECT_ROOT}/src/network)
install_hfile(${PROJECT_ROOT}/src/outcome)
install_hfile(${PROJECT_ROOT}/src/processing)
install_hfile(${PROJECT_ROOT}/src/primitives)
install_hfile(${PROJECT_ROOT}/src/runtime)
install_hfile(${PROJECT_ROOT}/src/scale)
install_hfile(${PROJECT_ROOT}/src/storage)
install_hfile(${PROJECT_ROOT}/src/subscription)
install_hfile(${PROJECT_ROOT}/src/transaction_pool)
install_hfile(${PROJECT_ROOT}/src/verification)
install_hfile(${PROJECT_ROOT}/src/account)
install_hfile(${PROJECT_ROOT}/app/integration)
install_hfile(${PROJECT_ROOT}/src/local_secure_storage)
install_hfile(${PROJECT_ROOT}/src/singleton)

# install proto header files
install_hfile(${CMAKE_CURRENT_BINARY_DIR}/generated/crdt)
install_hfile(${CMAKE_CURRENT_BINARY_DIR}/generated/processing)
install_hfile(${CMAKE_CURRENT_BINARY_DIR}/generated/account)
install_hfile(${CMAKE_CURRENT_BINARY_DIR}/generated/blockchain)

# install the configuration file
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/SuperGeniusConfig.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SuperGenius
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/SuperGeniusConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SuperGenius
)

