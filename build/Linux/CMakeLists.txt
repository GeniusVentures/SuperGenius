cmake_minimum_required(VERSION 3.12)

# --------------------------------------------------------
# if on Linux system, build the Linux executables
if (NOT ${CMAKE_HOST_SYSTEM_NAME} MATCHES "Linux")
    message(FATAL_ERROR "${CMAKE_HOST_SYSTEM_NAME} host system name does not match Linux - Please select the correct folder for configuring project")
endif()

# --------------------------------------------------------
# Project
project(SuperGenius C CXX)

# --------------------------------------------------------
# include common compiler options
include(../CommonCompilerOptions.CMake)

# --------------------------------------------------------
# Set toolchain file
set(CMAKE_TOOLCHAIN_FILE "${PROJECT_ROOT}/cmake/toolchain/cxx17.cmake")

# --------------------------------------------------------
# Set OS threading settings
set(CMAKE_THREAD_LIBS_INIT "-lpthread")
set(CMAKE_HAVE_THREADS_LIBRARY ON)
set(CMAKE_USE_WIN32_THREADS_INIT OFF)
set(CMAKE_USE_PTHREADS_INIT ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)

# --------------------------------------------------------
# Allow multiple definitions
set(FORCE_MULTILE ON)
set(MULTIPLE_OPTION "-z muldefs")

# --------------------------------------------------------
# Set extra compiler flags
set(EXTRA_CXX_FLAGS)

# --------------------------------------------------------
# Set custom subfolder for ABI
set(ABI_SUBFOLDER_NAME "")

# --------------------------------------------------------
# Include common build parameters
include(../CommonBuildParameters.cmake)
