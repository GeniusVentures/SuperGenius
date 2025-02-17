cmake_minimum_required(VERSION 3.22)

if(POLICY CMP0025)
    # NEW: report OS X version of Clang as "AppleClang"
    cmake_policy(SET CMP0025 NEW)
endif()

if(POLICY CMP0042)
    # NEW: set TARGET PROPERTY MACOSX_RPATH to on by default
    cmake_policy(SET CMP0042 NEW)
endif()

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(NAMED_LANGUAGE_SUPPORT OFF CACHE BOOL "Toolchain named language support parameter")
    set(ENABLE_BITCODE OFF CACHE BOOL "Toolchain bitcode enable parameter")
    set(ENABLE_ARC OFF CACHE BOOL "Toolchain enable arc parameter")
    set(ENABLE_VISIBILITY ON CACHE BOOL "Toolchain visibility parameter")
    set(ENABLE_STRICT_TRY_COMPILE OFF CACHE BOOL "Toolchain try_compile enable parameter")
    set(PLATFORM OS64 CACHE STRING "Toolchain platform parameter")
    set(DEPLOYMENT_TARGET "16" CACHE STRING "Minimum iOS version")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../apple.toolchain.cmake" CACHE STRING "Toolchain file")
endif()

# Project
project(SuperGenius LANGUAGES C CXX)

# include common compiler options
include(../CommonCompilerOptions.cmake)

# Turn off examples
set(BUILD_EXAMPLES OFF)

# Set host system protoc executable path
set(PROTOC_EXECUTABLE "${_THIRDPARTY_BUILD_DIR}/protobuf_host/bin/protoc${CMAKE_EXECUTABLE_SUFFIX}")

# Set custom protobuf path for iOS
set(iOS_Protobuf_DIR "${THIRDPARTY_BUILD_DIR}/protobuf/lib/cmake/protobuf")
set(iOS_Protobuf_INCLUDE_DIR "${THIRDPARTY_BUILD_DIR}/protobuf/include/google/protobuf")
set(iOS_Protobuf_LIBRARIES "${THIRDPARTY_BUILD_DIR}/protobuf/lib")

# Set OS threading settings
set(CMAKE_THREAD_LIBS_INIT "-lpthread")
set(CMAKE_HAVE_THREADS_LIBRARY ON)
set(CMAKE_USE_WIN32_THREADS_INIT OFF)
set(CMAKE_USE_PTHREADS_INIT ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)

# Allow multiple definitions
set(FORCE_MULTILE ON)
set(MULTIPLE_OPTION "-Xlink=-force:multiple")

# Set custom subfolder for ABI
set(ABI_SUBFOLDER_NAME "/aarch64")

set(TESTING OFF)

# Include common build paramaeters
include(../CommonBuildParameters.cmake)
