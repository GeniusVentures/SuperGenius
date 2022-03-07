cmake_minimum_required(VERSION 3.12)

# --------------------------------------------------------
# if on Linux system, build the Android executables
if (NOT ${CMAKE_HOST_SYSTEM_NAME} MATCHES "Linux")
    message(FATAL_ERROR "${CMAKE_HOST_SYSTEM_NAME} host system name does not match Linux for Android cross compiling - Please select the correct folder for configuring project")
endif()

# --------------------------------------------------------
# Set cross compiling settings
set(CMAKE_SYSTEM_NAME Android)
set(CMAKE_SYSTEM_VERSION 28) # API level
set(ANDROID_NATIVE_API_LEVEL 28)

# --------------------------------------------------------
# Check Android NDK version
if(NOT CMAKE_ANDROID_NDK)
	message(FATAL_ERROR "Cannot find Android NDK, make sure you pass -DCMAKE_ANDROID_NDK=[path_to_NDK]")
endif()

file(READ "${CMAKE_ANDROID_NDK}/source.properties" SOURCE_PROPERTIES_CONTENT)
string(REGEX MATCH "Pkg\\.Revision = ([0-9]+)\\.([0-9]+)\\." NDK_VERSION_MATCH "${SOURCE_PROPERTIES_CONTENT}")
set(CMAKE_ANDROID_NDK_VERSION ${CMAKE_MATCH_1})
set(CMAKE_ANDROID_NDK_VERSION_MINOR ${CMAKE_MATCH_2})

message("--Android NDK version: ${CMAKE_ANDROID_NDK_VERSION}.${CMAKE_ANDROID_NDK_VERSION_MINOR}")

if (NOT CMAKE_ANDROID_NDK_VERSION EQUAL 23)
  message(FATAL_ERROR "Android NDK LTS Version (r23b) stable long term support version required")
endif()

# --------------------------------------------------------
# Check required input args
if(NOT ANDROID_ABI)
	message(FATAL_ERROR "Cannot find ANDROID_ABI, make sure you pass -DANDROID_ABI=[armeabi-v7a | arm64-v8a | x86 | x86_64]")
endif()
set(CMAKE_ANDROID_ARCH_ABI ${ANDROID_ABI})

if(NOT ANDROID_TOOLCHAIN)
	message(FATAL_ERROR "Cannot find ANDROID_TOOLCHAIN, make sure you pass -DANDROID_TOOLCHAIN=[e.g clang]")
endif()

# --------------------------------------------------------
# Config param for android
set(CMAKE_ANDROID_STL_TYPE c++_static)

# --------------------------------------------------------
# Set toolchain file
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_ANDROID_NDK}/build/cmake/android.toolchain.cmake")
if(NOT EXISTS ${CMAKE_TOOLCHAIN_FILE})
  message(FATAL_ERROR "Android NDK toolchain file ${CMAKE_TOOLCHAIN_FILE} not found")
endif()

# --------------------------------------------------------
# Project
project(SuperGenius C CXX)

# --------------------------------------------------------
# include common compiler options
include(../CommonCompilerOptions.CMake)

# --------------------------------------------------------
# Turn off examples
set(BUILD_EXAMPLES OFF)

# --------------------------------------------------------
# Set custom subfolder for ABI
set(ABI_SUBFOLDER_NAME "/${ANDROID_ABI}")

# --------------------------------------------------------
# Set host system protoc executable path
set(PROTOC_EXECUTABLE "${THIRDPARTY_BUILD_DIR}/protobuf/bin/protoc")

# --------------------------------------------------------
# Allow multiple definitions
set(FORCE_MULTILE OFF)

# --------------------------------------------------------
# Set extra compiler flags
set(EXTRA_CXX_FLAGS -Wno-error)

set(TESTING OFF)

# --------------------------------------------------------
# Include common build parameters
include(../CommonBuildParameters.cmake)

message("Boost_LIBRARIES: ${Boost_LIBRARIES}")
