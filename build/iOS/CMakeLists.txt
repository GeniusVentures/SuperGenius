
cmake_minimum_required(VERSION 3.5.1)
if(POLICY CMP0025)
	#NEW: report OS X version of Clang as "AppleClang"
  cmake_policy(SET CMP0025 NEW)
endif()
if(POLICY CMP0042)
  #NEW: set TARGET PROPERTY MACOSX_RPATH to on by default
  cmake_policy(SET CMP0042 NEW)
endif()

function(print)
  message(STATUS "[${CMAKE_PROJECT_NAME}] ${ARGV}")
endfunction()

message("---${CMAKE_HOST_SYSTEM_NAME}")

set(IOS_DEPLOYMENT_TARGET 10.12)

# Project
project(SuperGenius C CXX)

if (${CMAKE_HOST_SYSTEM_NAME} MATCHES "Darwin")

  set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM "BOTH")
  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
  set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
  set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MTd")

  if (CMAKE_BUILD_TYPE EQUAL Debug)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MTd")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MTd")
  endif (CMAKE_BUILD_TYPE EQUAL Debug)

  if (CMAKE_BUILD_TYPE EQUAL Release)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MT")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MT")
  endif (CMAKE_BUILD_TYPE EQUAL Release)

  set(CompilerFlags
      CMAKE_CXX_FLAGS
      CMAKE_CXX_FLAGS_DEBUG
      CMAKE_CXX_FLAGS_RELEASE
      CMAKE_C_FLAGS
      CMAKE_C_FLAGS_DEBUG
      CMAKE_C_FLAGS_RELEASE
      )
  foreach(CompilerFlag ${CompilerFlags})
    string(REPLACE "/MD" "/MT" ${CompilerFlag} "${${CompilerFlag}}")
  endforeach()

  print("C flags: ${CMAKE_C_FLAGS}")
  print("CXX flags: ${CMAKE_CXX_FLAGS}")
  print("C Debug flags: ${CMAKE_C_FLAGS_DEBUG}")
  print("CXX Debug flags: ${CMAKE_CXX_FLAGS_DEBUG}")
  print("C Release flags: ${CMAKE_C_FLAGS_RELEASE}")
  print("CXX Release flags: ${CMAKE_CXX_FLAGS_RELEASE}")

	set(CMAKE_SYSTEM_NAME "iOS")
  set(_CMAKE_COMMON_BUILD_PARAMS
		-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
		-DCMAKE_SYSTEM_NAME:STRING=${CMAKE_SYSTEM_NAME}
		-DCMAKE_TOOLCHAIN_FILE:PATH=${CMAKE_TOOLCHAIN_FILE}
		-DIOS_PLATFORM:STRING=${IOS_PLATFORM}
		-DIOS_ARCH:STRING=${IOS_ARCH}
		-DENABLE_ARC:STRING="0"
		-DENABLE_BITCODE:STRING=NO
		-DENABLE_VISIBILITY:STRING=${ENABLED_VISIBILITY}
		-DCMAKE_OSX_ARCHITECTURES:STRING=${CMAKE_OSX_ARCHITECTURES}
		-DIOS_SDK_VERSION:STRING=${IOS_SDK_VERSION}
		-DIOS_DEPLOYMENT_TARGET:STRING=${IOS_DEPLOYMENT_TARGET}
		-DCMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH:STRING=YES
	#	-DCMAKE_SYSTEM_PROCESSOR:STRING=${CMAKE_SYSTEM_PROCESSOR}
	)

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
  set(_FINDPACKAGE_PROTOBUF_CONFIG_DIR "${_THIRDPARTY_BUILD_DIR}/protobuf/lib/cmake/protobuf")
  set(_FINDPACKAGE_PROTOBUF_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/protobuf/include/google/protobuf")
  set(_FINDPACKAGE_PROTOBUF_LIB_DIR "${_THIRDPARTY_BUILD_DIR}/protobuf/lib")
  set(_FINDPACKAGE_PROTOBUF_PROTOC_EXEC "${_THIRDPARTY_BUILD_DIR}/protobuf-host/bin/protoc")

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

  # Set config of ipfs-pubsub
  set(_FINDPACKAGE_IPFS_PUBSUB_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/ipfs-pubsub/include")
  set(_FINDPACKAGE_IPFS_PUBSUB_DIR "${_THIRDPARTY_BUILD_DIR}/ipfs-pubsub/lib/cmake/ipfs-pubsub")

  set(_FINDPACKAGE_BOOST_DI_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/Boost.DI/include")

  set(_THIRDPARTY_DIR "${_THIRDPARTY_BUILD_DIR}/../..")
  set(_BOOST_ROOT "${_THIRDPARTY_DIR}/boost/build/iOS/${iOS_ABI}")
  set(_Boost_INCLUDE_DIR "${_BOOST_ROOT}/include")
  set(boost_headers_DIR "${_BOOST_ROOT}/lib/cmake/boost_headers-1.72.0")
  set(boost_random_DIR "${_BOOST_ROOT}/lib/cmake/boost_random-1.72.0")
  set(boost_system_DIR "${_BOOST_ROOT}/lib/cmake/boost_system-1.72.0")
  set(boost_filesystem_DIR "${_BOOST_ROOT}/lib/cmake/boost_filesystem-1.72.0")
  set(boost_program_options_DIR "${_BOOST_ROOT}/lib/cmake/boost_program_options-1.72.0")
	set(boost_date_time_DIR "${_BOOST_ROOT}/lib/cmake/boost_date_time-1.72.0")
	set(boost_regex_DIR "${_BOOST_ROOT}/lib/cmake/boost_regex-1.72.0")
	set(boost_atomic_DIR "${_BOOST_ROOT}/lib/cmake/boost_atomic-1.72.0")
	set(boost_chrono_DIR "${_BOOST_ROOT}/lib/cmake/boost_chrono-1.72.0")
  set(boost_log_DIR "${_BOOST_ROOT}/lib/cmake/boost_log-1.72.0")
  set(boost_log_setup_DIR "${_BOOST_ROOT}/lib/cmake/boost_log_setup-1.72.0")
  set(boost_thread_DIR "${_BOOST_ROOT}/lib/cmake/boost_thread-1.72.0")

  set(boost_DIR "${_BOOST_ROOT}/lib/cmake/Boost-1.72.0")

  set(_BOOST_CACHE_ARGS
        -DBOOST_LIB_DIR:PATH=${_BOOST_ROOT}/lib
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
        -DBoost_USE_STATIC_RUNTIME:BOOL=OFF
        -DBoost_USE_STATIC_LIBS:BOOL=ON
        -DBoost_NO_SYSTEM_PATHS:BOOL=ON
        -DCMAKE_THREAD_LIBS_INIT:STRING="-lpthread"
        -DCMAKE_HAVE_THREADS_LIBRARY:BOOL=ON
        -DCMAKE_USE_WIN32_THREADS_INIT:BOOL=OFF
        -DCMAKE_USE_PTHREADS_INIT:BOOL=ON
        -DTHREADS_PREFER_PTHREAD_FLAG:BOOL=ON
  )

  # build openssl
  set(_openssl_BUILD_ROOT "${_THIRDPARTY_DIR}/openssl/build/iOS/${iOS_ABI}")
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
          -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/SuperGenius
          ${_CMAKE_COMMON_BUILD_PARAMS}
          ${_OPENSSL_CACHE_ARGS}
          -DGTest_DIR:PATH=${_FINDPACKAGE_GTEST_CONFIG_DIR}
          -DBoost.DI_DIR:PATH=${_FINDPACKAGE_BOOST_DI_DIR}
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
          -DCMAKE_CXX_FLAGS:STRING=${CMAKE_CXX_FLAGS}\ -std=c++17\ -I\ ${_Boost_INCLUDE_DIR}\ -I\ ${_FINDPACKAGE_GSL_INCLUDE_DIR}\ -I\ ${_FINDPACKAGE_SPDLOG_INCLUDE_DIR}\ -I\ ${_OPENSSL_INCLUDE_DIR}\ -I\ ${_FINDPACKAGE_PROTOBUF_INCLUDE_DIR}\ -I\ ${_FINDPACKAGE_BOOST_DI_INCLUDE_DIR}\ -I\ ${_FINDPACKAGE_IPFS_LITE_CPP_INCLUDE_DIR}\ -I\ ${_FINDPACKAGE_IPFS_PUBSUB_INCLUDE_DIR}\ -I\ ${_FINDPACKAGE_LIBP2P_INCLUDE_DIR}\ -I\ ${_FINDPACKAGE_BOOST_DI_INCLUDE_DIR}\ -I\ ${_FINDPACKAGE_ROCKSDB_INCLUDE_DIR}\ -I\ ${_FINDPACKAGE_jsonrpc_lean_INCLUDE_DIR}\ -I\ ${_FINDPACKAGE_RapidJSON_INCLUDE_DIR}
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
          -DFORCE_MULTILE:BOOL=TRUE
          -DMULTIPLE_OPTION:STRING="-Xlink=-force:multiple"
  )
endif()
