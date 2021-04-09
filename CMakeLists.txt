cmake_minimum_required(VERSION 3.12)
set(
    CMAKE_TOOLCHAIN_FILE
    "${CMAKE_SOURCE_DIR}/cmake/toolchain/cxx17.cmake"
    CACHE
    FILEPATH
    "Default toolchain"
)
# Project
project(SuperGenius C CXX)
ADD_DEFINITIONS(-D_HAS_AUTO_PTR_ETC=1)
if (NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
  # https://cgold.readthedocs.io/en/latest/tutorials/toolchain/globals/cxx-standard.html#summary
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)
endif ()

include(cmake/functions.cmake)
include(cmake/print.cmake)

print("C flags: ${CMAKE_C_FLAGS}")
print("CXX flags: ${CMAKE_CXX_FLAGS}")
print("C Debug flags: ${CMAKE_C_FLAGS_DEBUG}")
print("CXX Debug flags: ${CMAKE_CXX_FLAGS_DEBUG}")
print("C Release flags: ${CMAKE_C_FLAGS_RELEASE}")
print("CXX Release flags: ${CMAKE_CXX_FLAGS_RELEASE}")
include(cmake/install.cmake)
# Package config ######################################################################################
set (CPACK_PACKAGE_VERSION_MAJOR "21")
set (CPACK_PACKAGE_VERSION_MINOR "0")
set (CPACK_PACKAGE_VERSION_PATCH "0")
set (CPACK_PACKAGE_VERSION_PRE_RELEASE "12")
set (CPACK_PACKAGE_VENDOR "Genius Ventures")

# Export flag    ######################################################################################
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CheckCXXCompilerFlag)

# Boost definition ######################################################################################
if (CMAKE_VERSION VERSION_LESS 3.11)
	# compatibility for boost import targets use bundled 3.11 FindBoost.cmake
	list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/legacyModules")
endif ()
option (SGNS_STACKTRACE_BACKTRACE "Use BOOST_STACKTRACE_USE_BACKTRACE in stacktraces, for POSIX" OFF)
if (SGNS_STACKTRACE_BACKTRACE)
	add_definitions(-DSGNS_STACKTRACE_BACKTRACE=1)
	if (BACKTRACE_INCLUDE)
		add_definitions(-DBOOST_STACKTRACE_BACKTRACE_INCLUDE_FILE=${BACKTRACE_INCLUDE})
	endif()
endif ()
include(CheckCXXCompilerFlag)
# add compilation flag for unix platform
include(build/compilation_flag_unix.cmake)
message("${CMAKE_CURRENT_SOURCE_DIR}/cmake/compile_option_by_platform/${CMAKE_SYSTEM_NAME}.cmake")
include(cmake/definition.cmake)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/compile_option_by_platform/${CMAKE_SYSTEM_NAME}.cmake")
  message("add compile option")
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/compile_option_by_platform/${CMAKE_SYSTEM_NAME}.cmake")
endif()

# add_compile_options(-zmuldefs)
# BOOST library 
set(Boost_USE_STATIC_LIBS ON) 
set(Boost_USE_MULTITHREADED ON)  
set(Boost_USE_STATIC_RUNTIME ON) 
find_package(Boost REQUIRED COMPONENTS date_time filesystem random regex system thread log log_setup program_options) # header only libraries must not be added here

set(Protobuf_USE_STATIC_LIBS ON)
find_package(Protobuf CONFIG REQUIRED PATHS ${Protobuf_DIR})

include_directories(
  # ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/src
  ${CBOR_INCLUDE_DIR}  
  ${rocksdb_INCLUDE_DIR}
  ${sr25519_INCLUDE_DIR}
  ${ed25519_INCLUDE_DIR}
  ${libsecp256k1_INCLUDE_DIR}
  ${binaryen_INCLUDE_DIR}
  ${binaryen_INCLUDE_DIR}/binaryen
  ${tsl_hat_trie_INCLUDE_DIR}
  ${ipfs-lite-cpp_INCLUDE_DIR}
)

find_package(Boost.DI CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(tsl_hat_trie CONFIG REQUIRED)
find_package(libp2p CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(rocksdb CONFIG REQUIRED)
find_package(ipfs-lite-cpp CONFIG REQUIRED)
find_package(ed25519 CONFIG REQUIRED)
find_package(sr25519 CONFIG REQUIRED)
find_package(RapidJSON CONFIG REQUIRED)
find_package(binaryen CONFIG REQUIRED)
find_package(libsecp256k1 CONFIG REQUIRED)
find_package(xxhash CONFIG REQUIRED)

option(TESTING "Build tests" ON)
link_directories(
  ${BOOST_LIB_DIR}
  ${ipfs-lite-cpp_LIB_DIR}
)

add_subdirectory(src)
add_subdirectory(node)


if (TESTING)
  find_package(GTest CONFIG REQUIRED)
  enable_testing()
  add_subdirectory(test)
endif ()
