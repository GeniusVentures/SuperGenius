cmake_minimum_required(VERSION 3.22)

# if on Linux system, build the Linux executables
if(NOT ${CMAKE_HOST_SYSTEM_NAME} MATCHES "Linux")
    message(FATAL_ERROR "${CMAKE_HOST_SYSTEM_NAME} host system name does not match Linux - Please select the correct folder for configuring project")
endif()

# Project
project(
  SuperGenius
  VERSION 0.1.0
  LANGUAGES C CXX
)

# include common compiler options
include(../CommonCompilerOptions.cmake)

# Set custom subfolder for ABI
set(ABI_SUBFOLDER_NAME "")

# Include common build parameters
include(../CommonBuildParameters.cmake)
