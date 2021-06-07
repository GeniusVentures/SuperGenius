
cmake_minimum_required(VERSION 3.5.1)

function(print)
  message(STATUS "[${CMAKE_PROJECT_NAME}] ${ARGV}")
endfunction()

# Project
project(SuperGenius C CXX)


project(ANDROID_SHARED_THIRD_PARTY)

set(CMAKE_SYSTEM_NAME Android)
set(CMAKE_SYSTEM_VERSION 28) # API level
set(ANDROID_NATIVE_API_LEVEL 28)

if(NOT CMAKE_ANDROID_NDK)
	message(FATAL_ERROR "Cannot find Android NDK, make sure you pass -DCMAKE_ANDROID_NDK=[path_to_NDK]")
endif()

file(READ "${CMAKE_ANDROID_NDK}/source.properties" SOURCE_PROPERTIES_CONTENT)
string(REGEX MATCH "Pkg\\.Revision = ([0-9]+)\\.([0-9]+)\\." NDK_VERSION_MATCH "${SOURCE_PROPERTIES_CONTENT}")
set(CMAKE_ANDROID_NDK_VERSION ${CMAKE_MATCH_1})
set(CMAKE_ANDROID_NDK_VERSION_MINOR ${CMAKE_MATCH_2})

message("--Android NDK version: ${CMAKE_ANDROID_NDK_VERSION}.${CMAKE_ANDROID_NDK_VERSION_MINOR}")

if (NOT CMAKE_ANDROID_NDK_VERSION EQUAL 21)
  message(FATAL_ERROR "Android NDK LTS Version (r21e) stable long term support version required")
endif()

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_ANDROID_NDK}/build/cmake/android.toolchain.cmake")
if(NOT EXISTS ${CMAKE_TOOLCHAIN_FILE})
  message(FATAL_ERROR "Android NDK toolchain file ${CMAKE_TOOLCHAIN_FILE} not found")
endif()

## Config param for android
set(_CMAKE_COMMON_BUILD_PARAMS
  -DCMAKE_SYSTEM_NAME:STRING=${CMAKE_SYSTEM_NAME}
  -DCMAKE_ANDROID_NDK:PATH=${CMAKE_ANDROID_NDK}
	-DANDROID_ABI:STRING=${ANDROID_ABI}
	-DANDROID_NATIVE_API_LEVEL:STRING=${ANDROID_NATIVE_API_LEVEL}
	-DANDROID_TOOLCHAIN:STRING=${ANDROID_TOOLCHAIN}
	-DANDROID_STL:STRING=c++_static
	-DCMAKE_TOOLCHAIN_FILE:PATH=${CMAKE_TOOLCHAIN_FILE}
	-DOPENSSL_USE_STATIC_LIBS:BOOL=TRUE
	)

print("C flags: ${CMAKE_C_FLAGS}")
print("CXX flags: ${CMAKE_CXX_FLAGS}")
print("C Debug flags: ${CMAKE_C_FLAGS_DEBUG}")
print("CXX Debug flags: ${CMAKE_CXX_FLAGS_DEBUG}")
print("C Release flags: ${CMAKE_C_FLAGS_RELEASE}")
print("CXX Release flags: ${CMAKE_CXX_FLAGS_RELEASE}")

# define third party directory
#set(THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${THIRDPARTY_DIR}")
set(_THIRDPARTY_BUILD_DIR "${THIRDPARTY_DIR}")
#set(_THIRDPARTY_SOURCE_DIR "${THIRDPARTY_DIR}/build/Windows")

print("${_THIRDPARTY_BUILD_DIR}")
include(ExternalProject)

# Note: All thirdparty projects should be prebuilt.
# Set config of GTest
set(_FINDPACKAGE_GTEST_CONFIG_DIR "${_THIRDPARTY_BUILD_DIR}/GTest/lib/cmake/GTest")

print(${_FINDPACKAGE_GTEST_CONFIG_DIR})

# Set config of protobuf project
set(_FINDPACKAGE_PROTOBUF_CONFIG_DIR "${_THIRDPARTY_BUILD_DIR}/grpc/lib/cmake/protobuf")
set(_FINDPACKAGE_PROTOBUF_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/grpc/include/google/protobuf")
set(_FINDPACKAGE_PROTOBUF_LIB_DIR "${_THIRDPARTY_BUILD_DIR}/grpc/lib")
set(_FINDPACKAGE_PROTOBUF_PROTOC_EXEC "${_THIRDPARTY_BUILD_DIR}/protobuf/bin/protoc")

# if OPENSSL_ROOT_DIR is set, propagate that hint path to the external projects with OpenSSL dependency.
set(_CMAKE_ARGS_OPENSSL_ROOT_DIR "")
if (OPENSSL_ROOT_DIR)
  set(_CMAKE_ARGS_OPENSSL_ROOT_DIR "-DOPENSSL_ROOT_DIR:PATH=${OPENSSL_ROOT_DIR}")
endif()

# Set config of rocksdb
set(_FINDPACKAGE_ROCKSDB_DIR "${_THIRDPARTY_BUILD_DIR}/rocksdb/lib/cmake/rocksdb")
set(_FINDPACKAGE_ROCKSDB_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/rocksdb/include")

# Set config of Microsoft.GSL
set(_FINDPACKAGE_GSL_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/GSL/include")

# Set config of spdlog v1.4.2
set(_FINDPACKAGE_SPDLOG_CONFIG_DIR "${_THIRDPARTY_BUILD_DIR}/spdlog/lib/cmake/spdlog")
set(_FINDPACKAGE_SPDLOG_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/spdlog/include")

# Set config of  tsl_hat_trie
set(_FINDPACKAGE_TSL_HAT_TRIE_CONFIG_DIR "${_THIRDPARTY_BUILD_DIR}/hat-trie/lib/cmake/tsl_hat_trie")
set(_FINDPACKAGE_TSL_HAT_TRIE_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/hat-trie/include")

# Set config of libp2p
set(_FINDPACKAGE_libp2p_CONFIG_DIR "${_THIRDPARTY_BUILD_DIR}/libp2p/lib/cmake/libp2p")
set(_FINDPACKAGE_libp2p_LIBRARY_DIR "${_THIRDPARTY_BUILD_DIR}/libp2p/lib")
set(_FINDPACKAGE_LIBP2P_INCLUDE_DIR    "${_THIRDPARTY_BUILD_DIR}/libp2p/include")

# Set config of ipfs-lite-cpp
set(_FINDPACKAGE_IPFS_LITE_CPP_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/ipfs-lite-cpp/include")
set(_FINDPACKAGE_IPFS_LITE_CPP_DIR "${_THIRDPARTY_BUILD_DIR}/ipfs-lite-cpp/cmake/ipfs-lite-cpp")

# Set config of ipfs-lite-cpp
set(_FINDPACKAGE_IPFS_PUBSUB_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/ipfs-pubsub/include")
set(_FINDPACKAGE_IPFS_PUBSUB_DIR "${_THIRDPARTY_BUILD_DIR}/ipfs-pubsub/lib/cmake/ipfs-pubsub")

set(_FINDPACKAGE_BOOST_DI_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/Boost.DI/include")

set(_THIRDPARTY_DIR "${_THIRDPARTY_BUILD_DIR}/../..")
set(_BOOST_ROOT "${_THIRDPARTY_DIR}/boost/build/Android")
set(_Boost_INCLUDE_DIR "${_BOOST_ROOT}/include")
set(_Boost_LIB_DIR "${_BOOST_ROOT}/libs/${ANDROID_ABI}")
set(boost_headers_DIR "${_Boost_LIB_DIR}/cmake/boost_headers-1.72.0")
set(boost_random_DIR "${_Boost_LIB_DIR}/cmake/boost_random-1.72.0")
set(boost_system_DIR "${_Boost_LIB_DIR}/cmake/boost_system-1.72.0")
set(boost_filesystem_DIR "${_Boost_LIB_DIR}/cmake/boost_filesystem-1.72.0")
set(boost_program_options_DIR "${_Boost_LIB_DIR}/cmake/boost_program_options-1.72.0")
set(boost_date_time_DIR "${_Boost_LIB_DIR}/cmake/boost_date_time-1.72.0")
set(boost_regex_DIR "${_Boost_LIB_DIR}/cmake/boost_regex-1.72.0")
set(boost_atomic_DIR "${_Boost_LIB_DIR}/cmake/boost_atomic-1.72.0")
set(boost_chrono_DIR "${_Boost_LIB_DIR}/cmake/boost_chrono-1.72.0")
set(boost_log_DIR "${_Boost_LIB_DIR}/cmake/boost_log-1.72.0")
set(boost_log_setup_DIR "${_Boost_LIB_DIR}/cmake/boost_log_setup-1.72.0")
set(boost_thread_DIR "${_Boost_LIB_DIR}/cmake/boost_thread-1.72.0")

set(boost_DIR "${_Boost_LIB_DIR}/cmake/Boost-1.72.0")

set(_BOOST_CACHE_ARGS
      -DBOOST_LIB_DIR:PATH="${_Boost_LIB_DIR}"
      -Dboost_headers_DIR:PATH=${boost_headers_DIR}
      -Dboost_random_DIR:PATH=${boost_random_DIR}
      -Dboost_system_DIR:PATH=${boost_system_DIR}
      -Dboost_filesystem_DIR:PATH=${boost_filesystem_DIR}
      -Dboost_program_options_DIR:PATH=${boost_program_options_DIR}
      -Dboost_date_time_DIR:PATH=${boost_date_time_DIR}
      -Dboost_regex_DIR:PATH=${boost_regex_DIR}
      -Dboost_atomic_DIR:PATH=${boost_atomic_DIR}
      -Dboost_chrono_DIR:PATH=${boost_chrono_DIR}
      -Dboost_log_DIR:PATH=${boost_log_DIR}
      -Dboost_log_setup_DIR:PATH=${boost_log_setup_DIR}
      -Dboost_thread_DIR:PATH=${boost_thread_DIR}
      -DBoost_DIR:PATH=${boost_DIR}
      -DBoost_INCLUDE_DIR:PATH=${_Boost_INCLUDE_DIR}
      -DBoost_USE_MULTITHREADED:BOOL=ON
      -DBoost_USE_STATIC_RUNTIME:BOOL=ON
      -DBoost_USE_STATIC_LIBS:BOOL=ON
      -DBoost_NO_SYSTEM_PATHS:BOOL=ON
)

# build openssl
set(_openssl_BUILD_ROOT "${_THIRDPARTY_DIR}/openssl/build/Android/${ANDROID_ABI}")
set(_OPENSSL_CACHE_ARGS
    -DOPENSSL_USE_STATIC_LIBS:BOOL=ON
    -DOPENSSL_ROOT_DIR:STRING=${_openssl_BUILD_ROOT}
    -DOPENSSL_INCLUDE_DIR:PATH=${_openssl_BUILD_ROOT}/include
    -DOPENSSL_LIBRARIES:PATH=${_openssl_BUILD_ROOT}/lib
    -DOPENSSL_CRYPTO_LIBRARY:PATH=${_openssl_BUILD_ROOT}/lib/libcrypto${CMAKE_STATIC_LIBRARY_SUFFIX}
    -DOPENSSL_SSL_LIBRARY:PATH=${_openssl_BUILD_ROOT}/lib/libssl${CMAKE_STATIC_LIBRARY_SUFFIX}
  )
set(_OPENSSL_INCLUDE_DIR ${_openssl_BUILD_ROOT}/include)
set(_OPENSSL_target openssl)

# Build SuperGenius

# Set ed25519 and sr25519 config path

set(_FINDPACKAGE_ed25519_CONFIG_DIR "${_THIRDPARTY_BUILD_DIR}/ed25519/lib/cmake/ed25519")
set(_FINDPACKAGE_sr25519-donna_CONFIG_DIR "${_THIRDPARTY_BUILD_DIR}/sr25519-donna/lib/cmake/sr25519-donna")

set(_sr25519-donna_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/sr25519-donna/include")
set(_ed25519_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/ed25519/include")

set(_FINDPACKAGE_BOOST_DI_DIR "${_THIRDPARTY_BUILD_DIR}/Boost.DI/lib/cmake/Boost.DI")

# Set RapidJSON config path

set(_FINDPACKAGE_RapidJSON_CONFIG_DIR "${_THIRDPARTY_BUILD_DIR}/rapidjson/lib/cmake/RapidJSON")
set(_FINDPACKAGE_RapidJSON_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/rapidjson/include")

#set jsonrpc-lean include path

set(_FINDPACKAGE_jsonrpc_lean_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../../../thirdparty/jsonrpc-lean/include")

# Set config of binaryen

set(_FINDPACKAGE_BINARYEN_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/binaryen/include")
set(_FINDPACKAGE_BINARYEN_LIBRARY_DIR "${_THIRDPARTY_BUILD_DIR}/binaryen/lib")
set(_FINDPACKAGE_BINARYEN_DIR "${_THIRDPARTY_BUILD_DIR}/binaryen/lib/cmake/binaryen")


# Set config of secp256k1
set(_FINDPACKAGE_SECP256K1_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/libsecp256k1/include")
set(_FINDPACKAGE_SECP256K1_LIBRARY_DIR "${_THIRDPARTY_BUILD_DIR}/libsecp256k1/lib")
set(_FINDPACKAGE_SECP256K1_DIR "${_THIRDPARTY_BUILD_DIR}/libsecp256k1/lib/cmake/libsecp256k1")

# Set config of secp256k1
set(_FINDPACKAGE_XXHASH_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/xxhash/include")
set(_FINDPACKAGE_XXHASH_LIBRARY_DIR "${_THIRDPARTY_BUILD_DIR}/xxhash/lib")
set(_FINDPACKAGE_XXHASH_DIR "${_THIRDPARTY_BUILD_DIR}/xxhash/lib/cmake/xxhash")

ExternalProject_Add(SuperGenius
  PREFIX SuperGenius
  SOURCE_DIR "${CMAKE_SOURCE_DIR}/../../../SuperGenius"
  CMAKE_CACHE_ARGS
        ${_CMAKE_COMMON_BUILD_PARAMS}
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/SuperGenius
        ${_OPENSSL_CACHE_ARGS}
        -DGTest_DIR:PATH=${_FINDPACKAGE_GTEST_CONFIG_DIR}
        -DBoost.DI_DIR:PATH=${_FINDPACKAGE_BOOST_DI_DIR}
        # -DBOOST_ROOT:STRING="${BOOST_ROOT}"
        # -DBOOST_INCLUDE_DIR:STRING="${BOOST_INCLUDE_DIR}"
        # -DBOOST_LIBRARY_DIR:STRING="${BOOST_LIBRARY_DIR}"
        ${_BOOST_CACHE_ARGS}
        -Dspdlog_DIR:PATH=${_FINDPACKAGE_SPDLOG_CONFIG_DIR}
        -Dtsl_hat_trie_DIR:PATH=${_FINDPACKAGE_TSL_HAT_TRIE_CONFIG_DIR}
        -Dtsl_hat_trie_INCLUDE_DIR:PATH=${_FINDPACKAGE_TSL_HAT_TRIE_INCLUDE_DIR}
        -Dlibp2p_DIR:PATH=${_FINDPACKAGE_libp2p_CONFIG_DIR}
        -DProtobuf_DIR:PATH=${_FINDPACKAGE_PROTOBUF_CONFIG_DIR}
        -DProtobuf_INCLUDE_DIR:PATH=${_FINDPACKAGE_PROTOBUF_INCLUDE_DIR}
        -DProtobuf_LIBRARIES:PATH=${_FINDPACKAGE_PROTOBUF_LIB_DIR}
        -DProtobuf_PROTOC_EXECUTABLE:PATH=${_FINDPACKAGE_PROTOBUF_PROTOC_EXEC}
        -DRocksDB_DIR:PATH=${_FINDPACKAGE_ROCKSDB_DIR}
        -Dipfs-lite-cpp_DIR:STRING=${_THIRDPARTY_BUILD_DIR}/ipfs-lite-cpp/cmake/ipfs-lite-cpp
        -Dipfs-lite-cpp_LIB_DIR:STRING=${_THIRDPARTY_BUILD_DIR}/ipfs-lite-cpp/lib
        -Dipfs-pubsub_DIR:STRING=${_FINDPACKAGE_IPFS_PUBSUB_DIR}
        -DTESTING:BOOL=${TESTING}
        -DCBOR_INCLUDE_DIR:STRING=${_THIRDPARTY_BUILD_DIR}/ipfs-lite-cpp/include/deps/tinycbor/src
        -DCMAKE_CXX_FLAGS:STRING=-I\ ${_Boost_INCLUDE_DIR}\ -I\ ${_FINDPACKAGE_GSL_INCLUDE_DIR}\ -I\ ${_FINDPACKAGE_SPDLOG_INCLUDE_DIR}\ -I\ \\\"${OPENSSL_ROOT_DIR}/include\\\"\ -I\ ${_FINDPACKAGE_PROTOBUF_INCLUDE_DIR}\ -I\ ${_FINDPACKAGE_BOOST_DI_INCLUDE_DIR}\ -I\ ${_FINDPACKAGE_IPFS_LITE_CPP_INCLUDE_DIR}\ -I\ ${_FINDPACKAGE_IPFS_PUBSUB_INCLUDE_DIR}\ -I\ ${_FINDPACKAGE_LIBP2P_INCLUDE_DIR}\ -I\ ${_FINDPACKAGE_BOOST_DI_INCLUDE_DIR}\ -I\ ${_FINDPACKAGE_ROCKSDB_INCLUDE_DIR}\ -I\ ${_FINDPACKAGE_jsonrpc_lean_INCLUDE_DIR}\ -I\ ${_FINDPACKAGE_RapidJSON_INCLUDE_DIR}\ -Wno-error #\ -std=c99
        # -DCMAKE_C_FLAGS:STRING=${CMAKE_C_FLAGS}
        # -DCMAKE_CXX_FLAGS_RELEASE:STRING=${CMAKE_CXX_FLAGS_RELEASE}
        # -DCMAKE_C_FLAGS_RELEASE:STRING=${CMAKE_C_FLAGS_RELEASE}
        # -DCMAKE_CXX_FLAGS_DEBUG:STRING=${CMAKE_CXX_FLAGS_DEBUG}
        # -DCMAKE_C_FLAGS_DEBUG:STRING=${CMAKE_C_FLAGS_DEBUG}
        -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
        -Ded25519_DIR:PATH=${_FINDPACKAGE_ed25519_CONFIG_DIR}
        -Dsr25519-donna_DIR:PATH=${_FINDPACKAGE_sr25519-donna_CONFIG_DIR}
        -Dsr25519-donna_INCLUDE_DIR:PATH=${_sr25519-donna_INCLUDE_DIR}
        -Ded25519_INCLUDE_DIR:PATH=${_ed25519_INCLUDE_DIR}
        -DRapidJSON_DIR:PATH=${_FINDPACKAGE_RapidJSON_CONFIG_DIR}
        -Dbinaryen_INCLUDE_DIR:PATH=${_FINDPACKAGE_BINARYEN_INCLUDE_DIR}
        -Dbinaryen_LIBRARIES:PATH=${_FINDPACKAGE_BINARYEN_LIBRARY_DIR}
        -Dbinaryen_DIR:PATH=${_FINDPACKAGE_BINARYEN_DIR}
        -Dlibsecp256k1_DIR:PATH=${_FINDPACKAGE_SECP256K1_DIR}
        -Dlibsecp256k1_INCLUDE_DIR:PATH=${_FINDPACKAGE_SECP256K1_INCLUDE_DIR}
        -Dxxhash_DIR:PATH=${_FINDPACKAGE_XXHASH_DIR}
        -DFORCE_MULTILE:BOOL=FALSE
        -DMULTIPLE_OPTION:STRING="-zmuldefs"
        # -DUSER_ENV_LIBRARY:STRING=userenv
        # -DWIN_SOCK_LIBRARY:STRING=wsock32 ws2_32
)
