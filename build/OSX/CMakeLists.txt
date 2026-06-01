cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(_PLATFORM "MAC_UNIVERSAL" CACHE STRING "Setting the default build for x86_64")

    set(NAMED_LANGUAGE_SUPPORT ON CACHE BOOL "Toolchain named language support parameter")
    set(ENABLE_BITCODE OFF CACHE BOOL "Toolchain bitcode enable parameter")
    set(ENABLE_ARC OFF CACHE BOOL "Toolchain enable arc parameter")
    set(ENABLE_VISIBILITY ON CACHE BOOL "Toolchain visibility parameter")
    set(ENABLE_STRICT_TRY_COMPILE OFF CACHE BOOL "Toolchain try_compile enable parameter")
    set(PLATFORM ${_PLATFORM} CACHE STRING "Toolchain platform parameter")
    set(DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum OSX version")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../apple.toolchain.cmake" CACHE STRING "Toolchain file")
endif()

include(../../cmake/version.cmake)

project(
    SuperGenius
    VERSION ${PROJECT_VERSION}
    LANGUAGES C CXX
)

include(../CommonCompilerOptions.cmake)

set(PROTOC_EXECUTABLE "${_THIRDPARTY_BUILD_DIR}/protobuf_host/bin/protoc${CMAKE_EXECUTABLE_SUFFIX}")

set(ABI_SUBFOLDER_NAME "")

set(Vulkan_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/moltenvk/build/include")
set(Vulkan_LIBRARY "${_THIRDPARTY_BUILD_DIR}/moltenvk/build/lib/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a")

include(../CommonBuildParameters.cmake)

# ─── macOS Keychain Unlock Target ────────────────────────────────────────────
# Unlocks the login keychain for 1 hour before running tests.
# Usage: ninja test-unlocked
# This prevents repeated password prompts when test binaries access crypto keys.
if(BUILD_TESTING)
    add_custom_target(test-unlocked
        COMMAND security unlock-keychain -p "$ENV{USER}" ~/Library/Keychains/login.keychain-db
        COMMAND security set-keychain-settings -t 3600 ~/Library/Keychains/login.keychain-db
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -j4
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Unlocking keychain (1hr timeout) and running tests..."
    )
endif()
