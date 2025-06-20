cmake_minimum_required(VERSION 3.22)

set(ANDROID_PLATFORM_LEVEL 28)
set(ANDROID_NATIVE_API_LEVEL ${ANDROID_PLATFORM_LEVEL})
set(ANDROID_PLATFORM ${ANDROID_PLATFORM_LEVEL})

if(NOT ANDROID_ABI)
    message(WARNING "ANDROID_ABI not provided. Setting to `arm64-v8a`.")
    set(ANDROID_ABI arm64-v8a)
endif()

if(NOT CMAKE_ANDROID_NDK)
    if(DEFINED ENV{ANDROID_NDK_HOME})
        message(WARNING "Cannot find Android NDK, setting to $ENV{ANDROID_NDK_HOME}")
        set(CMAKE_ANDROID_NDK $ENV{ANDROID_NDK_HOME})
    else()
        message(FATAL_ERROR "Cannot find Android NDK, make sure you pass -DCMAKE_ANDROID_NDK=[path_to_NDK].")
    endif()
endif()

set(ANDROID_USE_LEGACY_TOOLCHAIN_FILE OFF CACHE BOOL "Disabling legacy toolchain behavior")

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_ANDROID_NDK}/build/cmake/android.toolchain.cmake")

set(Boost_COMPILER "clang")

# Android Needs this for find_library with HINTS
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)

include(../../cmake/version.cmake)

project(
    SuperGenius
    VERSION ${PROJECT_VERSION}
    LANGUAGES C CXX
)

# Set custom subfolder for ABI
set(ABI_SUBFOLDER_NAME "/${ANDROID_ABI}")

# include common compiler options
include(../CommonCompilerOptions.cmake)

# Turn off examples
set(BUILD_EXAMPLES OFF)

# Set host system protoc executable path
set(PROTOC_EXECUTABLE "${_THIRDPARTY_BUILD_DIR}/protobuf_host/bin/protoc${CMAKE_EXECUTABLE_SUFFIX}")

set(TESTING OFF)

# Include common build parameters
include(../CommonBuildParameters.cmake)

message("Boost_LIBRARIES: ${Boost_LIBRARIES}")
