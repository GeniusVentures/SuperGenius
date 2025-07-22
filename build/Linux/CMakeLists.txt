cmake_minimum_required(VERSION 3.22)

# if on Linux system, build the Linux executables
if(NOT ${CMAKE_HOST_SYSTEM_NAME} MATCHES "Linux")
    message(FATAL_ERROR "${CMAKE_HOST_SYSTEM_NAME} host system name does not match Linux - Please select the correct folder for configuring project")
endif()

include(../../cmake/version.cmake)
# Project
project(
    SuperGenius
    VERSION ${PROJECT_VERSION}
    LANGUAGES C CXX
)

set(ARCH ${CMAKE_SYSTEM_PROCESSOR})

# Normalize architecture names
if(ARCH MATCHES "(x86_64|amd64|AMD64)")
    set(ARCH "x86_64")
elseif(ARCH MATCHES "(aarch64|arm64|ARM64)")
    set(ARCH "aarch64")
else()
    message(WARNING "Unknown Linux architecture: ${ARCH}, using as-is")
endif()

set(ABI_SUBFOLDER_NAME "/${ARCH}")

# include common compiler options
include(../CommonCompilerOptions.cmake)

# Include common build parameters
include(../CommonBuildParameters.cmake)
