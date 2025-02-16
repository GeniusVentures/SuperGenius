# Set PROJECT_ROOT folder
get_filename_component(CURRENT_SOURCE_PARENT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY ABSOLUTE)
get_filename_component(PROJECT_ROOT "${CURRENT_SOURCE_PARENT}" DIRECTORY ABSOLUTE)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Package config
set(CPACK_PACKAGE_VERSION_MAJOR "21")
set(CPACK_PACKAGE_VERSION_MINOR "0")
set(CPACK_PACKAGE_VERSION_PATCH "0")
set(CPACK_PACKAGE_VERSION_PRE_RELEASE "12")
set(CPACK_PACKAGE_VENDOR "Genius Ventures")

set(CMAKE_INSTALL_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/SuperGenius)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (DEFINED SANITIZE_CODE AND "${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=${SANITIZE_CODE}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=${SANITIZE_CODE}")
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=${SANITIZE_CODE}")
    SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=${SANITIZE_CODE}")
    add_compile_options("-fsanitize=${SANITIZE_CODE}")
    add_link_options("-fsanitize=${SANITIZE_CODE}")
endif()

include(GNUInstallDirs)
include(GenerateExportHeader)
include(CMakePackageConfigHelpers)
include(CheckCXXCompilerFlag)
include(${PROJECT_ROOT}/cmake/functions.cmake)
include(${PROJECT_ROOT}/cmake/install.cmake)
include(${PROJECT_ROOT}/cmake/definition.cmake)
include(${PROJECT_ROOT}/build/CompilationFlags.cmake)

# Common compile options

# OS specific compile options
if(EXISTS "${PROJECT_ROOT}/cmake/compile_option_by_platform/${CMAKE_SYSTEM_NAME}.cmake")
    print("add compile option: ${PROJECT_ROOT}/cmake/compile_option_by_platform/${CMAKE_SYSTEM_NAME}.cmake")
    include("${PROJECT_ROOT}/cmake/compile_option_by_platform/${CMAKE_SYSTEM_NAME}.cmake")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${EXTRA_CXX_FLAGS}")

#define zkllvm directory
cmake_minimum_required(VERSION 3.19)

cmake_minimum_required(VERSION 3.19)

if(NOT DEFINED ZKLLVM_DIR)
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../zkLLVM/build/${BUILD_PLATFORM_NAME}/Release/${ANDROID_ABI}")
        message(STATUS "Setting default zkLLVM directory")
        get_filename_component(BUILD_PLATFORM_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
        set(ZKLLVM_DIR "${CMAKE_CURRENT_LIST_DIR}/../../zkLLVM/build/${BUILD_PLATFORM_NAME}/Release/${ANDROID_ABI}" CACHE STRING "Default zkLLVM Library")

        # Get absolute path
        cmake_path(SET ZKLLVM_DIR NORMALIZE "${ZKLLVM_DIR}")
    else()
        message(STATUS "zkLLVM directory not found, fetching latest release...")

        # Define GitHub repository information
        set(GITHUB_REPO "GeniusVentures/zkLLVM")
        set(GITHUB_API_URL "https://api.github.com/repos/${GITHUB_REPO}/releases")

        # Detect platform
        get_filename_component(BUILD_PLATFORM_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)

        # Define the target branch
        set(TARGET_BRANCH "develop")

        # Construct the release download URL
        if(ANDROID)
            set(ZKLLVM_ARCHIVE_NAME "${BUILD_PLATFORM_NAME}-${ANDROID_ABI}-${TARGET_BRANCH}-Release.tar.gz")
            set(ZKLLVM_RELEASE_URL "https://github.com/${GITHUB_REPO}/releases/download/${BUILD_PLATFORM_NAME}-${ANDROID_ABI}-${TARGET_BRANCH}-Release/${ZKLLVM_ARCHIVE_NAME}")
        else()
            set(ZKLLVM_ARCHIVE_NAME "${BUILD_PLATFORM_NAME}-${TARGET_BRANCH}-Release.tar.gz")
            set(ZKLLVM_RELEASE_URL "https://github.com/${GITHUB_REPO}/releases/download/${BUILD_PLATFORM_NAME}-${TARGET_BRANCH}-Release/${ZKLLVM_ARCHIVE_NAME}")
        endif()

        set(ZKLLVM_ARCHIVE "${CMAKE_BINARY_DIR}/${ZKLLVM_ARCHIVE_NAME}")
        set(ZKLLVM_EXTRACT_DIR "${CMAKE_CURRENT_LIST_DIR}/../../zkLLVM")

        # Download the latest release
        execute_process(
            COMMAND curl -L -o ${ZKLLVM_ARCHIVE} ${ZKLLVM_RELEASE_URL}
            RESULT_VARIABLE DOWNLOAD_RESULT
        )

        if(NOT DOWNLOAD_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to download zkLLVM archive from ${ZKLLVM_RELEASE_URL}")
        endif()

        file(MAKE_DIRECTORY ${ZKLLVM_EXTRACT_DIR})
        # Extract the archive to the correct location
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf ${ZKLLVM_ARCHIVE}
            WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../../zkLLVM/
            RESULT_VARIABLE EXTRACT_RESULT
        )

        if(NOT EXTRACT_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract zkLLVM archive")
        endif()

        # Set extracted directory as ZKLLVM_DIR
        set(ZKLLVM_DIR "${ZKLLVM_EXTRACT_DIR}/build/${BUILD_PLATFORM_NAME}/Release/${ANDROID_ABI}" CACHE STRING "Downloaded zkLLVM Library")

        message(STATUS "zkLLVM downloaded and extracted to ${ZKLLVM_DIR}")
    endif()
endif()

# define third party directory
if(NOT DEFINED THIRDPARTY_DIR)
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../thirdparty/README.md")
        print("Setting default third party directory")
        set(THIRDPARTY_DIR "${CMAKE_CURRENT_LIST_DIR}/../../thirdparty")

        # # get absolute path
        cmake_path(SET THIRDPARTY_DIR NORMALIZE "${THIRDPARTY_DIR}")
    else()
        message(FATAL_ERROR "Cannot find thirdparty directory required to build")
    endif()
endif()

if(NOT DEFINED CMAKE_BUILD_TYPE)
    message("CMAKE_BUILD_TYPE not defined, setting to release mode")
    set(CMAKE_BUILD_TYPE "Release")
endif()

if(NOT DEFINED THIRDPARTY_BUILD_DIR)
    print("Setting third party build directory default")
    get_filename_component(BUILD_PLATFORM_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
    set(THIRDPARTY_BUILD_DIR "${THIRDPARTY_DIR}/build/${BUILD_PLATFORM_NAME}/${CMAKE_BUILD_TYPE}")

    if(DEFINED ANDROID_ABI)
        set(THIRDPARTY_BUILD_DIR "${THIRDPARTY_BUILD_DIR}/${ANDROID_ABI}")
    endif()
endif()

set(_THIRDPARTY_BUILD_DIR "${THIRDPARTY_BUILD_DIR}")
print("THIRDPARTY BUILD DIR: ${_THIRDPARTY_BUILD_DIR}")

option(TESTING "Build tests" ON)
option(BUILD_EXAMPLES "Build examples" ON)
