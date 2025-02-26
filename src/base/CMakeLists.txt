add_library(hexutil
    hexutil.hpp
    hexutil.cpp
)
target_link_libraries(hexutil
    PUBLIC
    outcome
)
supergenius_install(hexutil)

add_library(buffer
    buffer.hpp
    buffer.cpp
)
target_link_libraries(buffer
    PUBLIC
    Boost::headers
    outcome
    PRIVATE
    hexutil
)
supergenius_install(buffer)

add_library(blob
    blob.hpp
    blob.cpp
)
target_link_libraries(blob
    PUBLIC
    hexutil
    Boost::headers
)
supergenius_install(blob)

add_library(logger
    logger.hpp
    logger.cpp
)
target_link_libraries(logger
    PUBLIC
    spdlog::spdlog
)
supergenius_install(logger)

add_library(mp_utils
    mp_utils.cpp
    mp_utils.hpp
)
target_link_libraries(mp_utils
    PUBLIC
    Boost::headers
)
supergenius_install(mp_utils)

add_library(fixed_point
    fixed_point.cpp
    fixed_point.hpp
)
target_link_libraries(fixed_point
    PUBLIC
    Boost::headers
    outcome
)
supergenius_install(fixed_point)



# Detect Git tag if available (exact match). If not on a tag, leave empty.
if(NOT DEFINED GIT_TAG OR "${GIT_TAG}" STREQUAL "")
    execute_process(
      COMMAND git describe --tags --exact-match
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      RESULT_VARIABLE _git_tag_result
      OUTPUT_VARIABLE _git_tag
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(_git_tag_result EQUAL 0)
        set(GIT_TAG "${_git_tag}")
    else()
        set(GIT_TAG "")
    endif()
endif()

# Detect current Git branch if not already set.
if(NOT DEFINED GIT_BRANCH OR "${GIT_BRANCH}" STREQUAL "")
    execute_process(
      COMMAND git rev-parse --abbrev-ref HEAD
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      OUTPUT_VARIABLE _git_branch
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(GIT_BRANCH "${_git_branch}")
endif()

# Generate build date if not provided.
if(NOT DEFINED BUILD_DATE OR "${BUILD_DATE}" STREQUAL "")
    string(TIMESTAMP BUILD_DATE "%Y%m%d")
endif()

# Get short Git commit hash if not provided.
if(NOT DEFINED GIT_COMMIT_HASH OR "${GIT_COMMIT_HASH}" STREQUAL "")
    execute_process(
      COMMAND git rev-parse --short HEAD
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      OUTPUT_VARIABLE _git_commit_hash
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(GIT_COMMIT_HASH "${_git_commit_hash}")
endif()

# --- Version Component Setup ---

# Set the version components (assumed to be defined from your project)
set(SUPERGENIUS_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(SUPERGENIUS_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(SUPERGENIUS_VERSION_PATCH ${PROJECT_VERSION_PATCH})

# Pre-release: if a Git tag exists, assume a release build; otherwise, use branch name.
if(GIT_TAG)
    set(SUPERGENIUS_VERSION_PRE_RELEASE "")
    set(SUPERGENIUS_VERSION_BUILD_METADATA "+${GIT_TAG}.${GIT_COMMIT_HASH}")
else()
    set(SUPERGENIUS_VERSION_PRE_RELEASE "-${GIT_BRANCH}")
    set(SUPERGENIUS_VERSION_BUILD_METADATA ".${GIT_COMMIT_HASH}")
endif()

# Build metadata: combine build date and commit hash.
set(SUPERGENIUS_RELEASE_DATE "${BUILD_DATE}")

# --- Output the detected values ---
message(STATUS "SUPERGENIUS_VERSION_MAJOR: ${SUPERGENIUS_VERSION_MAJOR}")
message(STATUS "SUPERGENIUS_VERSION_MINOR: ${SUPERGENIUS_VERSION_MINOR}")
message(STATUS "SUPERGENIUS_VERSION_PATCH: ${SUPERGENIUS_VERSION_PATCH}")
message(STATUS "GIT_TAG: ${GIT_TAG}")
message(STATUS "GIT_BRANCH: ${GIT_BRANCH}")
message(STATUS "BUILD_DATE: ${BUILD_DATE}")
message(STATUS "GIT_COMMIT_HASH: ${GIT_COMMIT_HASH}")
message(STATUS "SUPERGENIUS_VERSION_PRE_RELEASE: ${SUPERGENIUS_VERSION_PRE_RELEASE}")
message(STATUS "SUPERGENIUS_VERSION_BUILD_METADATA: ${SUPERGENIUS_VERSION_BUILD_METADATA}")

message(STATUS "Generating sgnsv.h from sgnsv.h.in")

cmake_policy(SET CMP0115 NEW)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sgnsv.h.in")
    message(FATAL_ERROR "sgnsv.h.in is missing in ${CMAKE_CURRENT_SOURCE_DIR}")
endif()


configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/sgnsv.h.in
    ${CMAKE_BINARY_DIR}/generated/base/sgnsv.h
    @ONLY
)

add_library(sgns_version
sgns_version.cpp
sgns_version.hpp
${CMAKE_BINARY_DIR}/generated/base/sgnsv.h
)

target_include_directories(sgns_version
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated/>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(sgns_version
    PUBLIC
)
supergenius_install(sgns_version)

