cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    # set(_PLATFORM "MAC_UNIVERSAL" CACHE STRING "Setting the build for both x86_64 and arm64")
    set(_PLATFORM "MAC" CACHE STRING "Setting the default build for x86_64")

    set(NAMED_LANGUAGE_SUPPORT OFF CACHE BOOL "Toolchain named language support parameter")
    set(ENABLE_BITCODE OFF CACHE BOOL "Toolchain bitcode enable parameter")
    set(ENABLE_ARC OFF CACHE BOOL "Toolchain enable arc parameter")
    set(ENABLE_VISIBILITY ON CACHE BOOL "Toolchain visibility parameter")
    set(ENABLE_STRICT_TRY_COMPILE OFF CACHE BOOL "Toolchain try_compile enable parameter")
    set(PLATFORM ${_PLATFORM} CACHE STRING "Toolchain platform parameter")
    set(DEPLOYMENT_TARGET "12.1" CACHE STRING "Minimum OSX version")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../apple.toolchain.cmake" CACHE STRING "Toolchain file")
endif()

project(SuperGenius LANGUAGES C CXX)

# include common compiler options
include(../CommonCompilerOptions.cmake)

# Set OS threading settings
set(CMAKE_THREAD_LIBS_INIT "-lpthread")
set(CMAKE_HAVE_THREADS_LIBRARY ON)
set(CMAKE_USE_WIN32_THREADS_INIT OFF)
set(CMAKE_USE_PTHREADS_INIT ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)

# Allow multiple definitions
set(FORCE_MULTILE ON)
set(MULTIPLE_OPTION "-Xlink=-force:multiple")

# Set extra compiler flags
set(EXTRA_CXX_FLAGS)

set(TESTING ON)

# Set custom subfolder for ABI
set(ABI_SUBFOLDER_NAME "")
set(Vulkan_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/moltenvk/build/include")
set(Vulkan_LIBRARY "${_THIRDPARTY_BUILD_DIR}/moltenvk/build/lib/MoltenVK.xcframework")
find_package(Vulkan REQUIRED)
set(VULKAN_LIBS Vulkan::Vulkan)

# Include common build parameters
include(../CommonBuildParameters.cmake)
