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
# Unlocks the login keychain and sets the partition list so all test binaries
# can access crypto keys without per-executable ACL prompts.
# Usage: ninja test-unlocked
#
# The issue: macOS keychain ACLs are per-application (by executable path).
# Each test binary is a separate app, so unlocking the keychain alone doesn't
# help — we also need to set the partition list to allow test binaries access.
#
# The fix: security set-key-partition-list -S apple-tool:,apple: allows all
# Apple crypto framework consumers (including our test binaries) to access
# the keychain without individual ACL prompts.
if(BUILD_TESTING)
    add_custom_target(test-unlocked
        # Unlock login keychain
        COMMAND security unlock-keychain ~/Library/Keychains/login.keychain-db
        # Set partition list: allow apple-tool: and apple: consumers (covers test binaries)
        COMMAND security set-key-partition-list -S apple-tool:,apple: -k "" ~/Library/Keychains/login.keychain-db
        # Run tests
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -j4
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Unlocking keychain and running tests..."
    )
endif()
