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
# Creates a temporary test keychain with no ACL restrictions, runs tests, then
# restores the original keychain search list.
# Usage: ninja test-unlocked
# This prevents repeated password prompts when test binaries access crypto keys
# (each executable has its own macOS ACL entry, so unlocking login keychain alone
# doesn't help — we need a keychain with no ACL at all).
if(BUILD_TESTING)
    set(TEST_KEYCHAIN "${CMAKE_BINARY_DIR}/sg_test.keychain-db")
    set(TEST_KEYCHAIN_PASSWORD "test-only-$(date +%s)")

    add_custom_target(test-unlocked
        # Save current keychain search list
        COMMAND bash -c "security list-keychains -d user | tr -d '\"' > ${CMAKE_BINARY_DIR}/.keychain_backup"
        # Create test keychain (replaces if exists)
        COMMAND bash -c "security delete-keychain ${TEST_KEYCHAIN} 2>/dev/null || true"
        COMMAND security create-keychain -p "${TEST_KEYCHAIN_PASSWORD}" ${TEST_KEYCHAIN}
        # Set test keychain as default + only keychain (no login keychain ACL prompts)
        COMMAND security list-keychains -d user -s ${TEST_KEYCHAIN}
        COMMAND security default-keychain -d user -s ${TEST_KEYCHAIN}
        COMMAND security unlock-keychain -p "${TEST_KEYCHAIN_PASSWORD}" ${TEST_KEYCHAIN}
        COMMAND security set-keychain-settings -t 3600 ${TEST_KEYCHAIN}
        # Run tests (test binaries will use test keychain, no ACL prompts)
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -j4
        # Restore original keychain search list
        COMMAND bash -c "ORIG=\$(cat ${CMAKE_BINARY_DIR}/.keychain_backup); security list-keychains -d user -s \$ORIG"
        COMMAND bash -c "security default-keychain -d user -s \$(head -1 ${CMAKE_BINARY_DIR}/.keychain_backup)"
        COMMAND bash -c "security delete-keychain ${TEST_KEYCHAIN} 2>/dev/null || true"
        COMMAND bash -c "rm -f ${CMAKE_BINARY_DIR}/.keychain_backup"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Creating test keychain (no ACL), running tests, restoring keychain..."
    )
endif()
