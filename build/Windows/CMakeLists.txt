cmake_minimum_required(VERSION 3.22)

# if on Windows system, build the windows executables
if(NOT ${CMAKE_HOST_SYSTEM_NAME} MATCHES "Windows")
    message(FATAL_ERROR "${CMAKE_HOST_SYSTEM_NAME} host system name does not match Windows - Please select the correct folder for configuring project")
endif()

# Project
project(SuperGenius LANGUAGES C CXX)

if(CMAKE_GENERATOR MATCHES "Visual Studio")
    if(NOT CMAKE_VS_GLOBALS MATCHES "(^|;)UseMultiToolTask=")
        list(APPEND CMAKE_VS_GLOBALS UseMultiToolTask=true)
    endif()

    if(NOT CMAKE_VS_GLOBALS MATCHES "(^|;)EnforceProcessCountAcrossBuilds=")
        list(APPEND CMAKE_VS_GLOBALS EnforceProcessCountAcrossBuilds=true)
    endif()
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded$<$<CONFIG:Debug>:Debug>)

# This is so that the SDK knows which version of Windows to target.
# Current set to Windows 10
add_compile_definitions(_WIN32_WINNT=0x0A00)

include(../CommonCompilerOptions.cmake)

set(Protobuf_DIR "${_THIRDPARTY_BUILD_DIR}/grpc/cmake")
set(Protobuf_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/grpc/include/google/protobuf")

# Set Windows crypto and socket library
set(USER_ENV_LIBRARY userenv)
set(WIN_API_LIBRARY ntdll)
set(WIN_SOCK_LIBRARY wsock32 ws2_32)
set(WIN_CRYPT_LIBRARY crypt32)

# Set custom subfolder for ABI
set(ABI_SUBFOLDER_NAME "")

# Include common build paramaeters
include(../CommonBuildParameters.cmake)
