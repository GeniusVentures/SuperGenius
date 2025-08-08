set(PROJECT_VERSION 1.0.0)

if(SGNS_NETWORK STREQUAL "dev" OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(DEV_NET)
endif()

message(STATUS "PROJECT_VERSION: ${PROJECT_VERSION}")

execute_process(
    COMMAND git describe --tags --exact-match
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    RESULT_VARIABLE _git_tag_result
    OUTPUT_VARIABLE _git_tag
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(_git_tag_result EQUAL 0)
    set(GIT_TAG "${_git_tag}" CACHE INTERNAL "Git tag" FORCE)
else()
    set(GIT_TAG "" CACHE INTERNAL "Git tag")
endif()

execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE _git_branch
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
set(GIT_BRANCH "${_git_branch}" CACHE INTERNAL "Git branch" FORCE)

string(TIMESTAMP BUILD_DATE "%Y%m%d")
set(BUILD_DATE "${BUILD_DATE}" CACHE INTERNAL "Build date" FORCE)

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE _git_commit_hash
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
set(GIT_COMMIT_HASH "${_git_commit_hash}" CACHE INTERNAL "Git commit hash" FORCE)
message(STATUS "GIT_COMMIT_HASH: ${GIT_COMMIT_HASH}")

if(GIT_TAG)
    message(STATUS "GIT_TAG: ${GIT_TAG}")
    set(SUPERGENIUS_VERSION_PRE_RELEASE "" CACHE INTERNAL "SuperGenius pre-release version" FORCE)
    set(SUPERGENIUS_VERSION_BUILD_METADATA "+${GIT_COMMIT_HASH}" CACHE INTERNAL "SuperGenius build metadata" FORCE)
else()
    message(STATUS "GIT_BRANCH: ${GIT_BRANCH}")
    set(SUPERGENIUS_VERSION_PRE_RELEASE "-${GIT_BRANCH}" CACHE INTERNAL "SuperGenius pre-release version" FORCE)
    set(SUPERGENIUS_VERSION_BUILD_METADATA ".${GIT_COMMIT_HASH}" CACHE INTERNAL "SuperGenius build metadata" FORCE)
endif()

set(SUPERGENIUS_RELEASE_DATE "${BUILD_DATE}" CACHE INTERNAL "SuperGenius release date" FORCE)
message(STATUS "BUILD_DATE: ${BUILD_DATE}")
