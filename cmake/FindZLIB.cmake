# Superbuild-local FindZLIB override (TEST-02 SC4 port, v1.1 Phase 12 plan
# 12-03; verbatim contract of thirdparty/sgns-blockstore/cmake/FindZLIB.cmake,
# whose dedicated CI is green against the same release artifact).
#
# Why this exists: cpp-httplib v0.53.1's installed config re-executes
# ZLIBConfig.cmake through CMake >= 3.26's hash-guarded find_dependency
# (it re-calls find_package(ZLIB) even when ZLIB_FOUND is already true),
# and ZLIBConfig.cmake's @PACKAGE_INIT@ macros (set_and_check /
# PACKAGE_PREFIX_DIR) then corrupt the enclosing httplibConfig.cmake's own
# header-path check with the zlib prefix ("File or directory
# <zlib>/include/httplib.h ... does not exist"). Any build that consumes
# the minio-cpp config package (whose miniocpp-config.cmake re-runs
# miniocpp-deps.cmake, which loads httplibConfig.cmake AND calls
# find_package(ZLIB) directly) MUST therefore resolve ZLIB without ever
# executing a ZLIB config file - and on Windows, where there is no system
# zlib, the builtin module-mode FindZLIB cannot see the artifact layout
# either (run 40826387117: "Could NOT find ZLIB (missing: ZLIB_LIBRARY)").
#
# This module imports the thirdparty-built zlib via its installed TARGETS
# file directly (no config-file execution, no artifact-name guessing) and
# exposes it under the canonical FindZLIB target name. The consumption
# block in build/CommonBuildParameters.cmake points
# ZLIB_SUPERBUILD_TARGETS_FILE into the extracted thirdparty artifact.

if(TARGET ZLIB::ZLIB)
    set(ZLIB_FOUND TRUE)
    return()
endif()

if(NOT DEFINED ZLIB_SUPERBUILD_TARGETS_FILE)
    message(FATAL_ERROR
        "FindZLIB shim: ZLIB_SUPERBUILD_TARGETS_FILE is not set. This "
        "override exists for builds that consume the minio-cpp config "
        "package (the cold-storage SGNS_COLD_STORAGE block in "
        "build/CommonBuildParameters.cmake sets it); unset "
        "CMAKE_MODULE_PATH to use CMake's builtin FindZLIB instead.")
endif()

if(NOT EXISTS "${ZLIB_SUPERBUILD_TARGETS_FILE}")
    message(FATAL_ERROR
        "FindZLIB shim: ${ZLIB_SUPERBUILD_TARGETS_FILE} does not exist - "
        "the thirdparty artifact extraction must complete before this "
        "build configures (check the Download thirdparty release step).")
endif()

# Imports ZLIB::ZLIBSTATIC (per-config location files are included by the
# targets file itself).
include("${ZLIB_SUPERBUILD_TARGETS_FILE}")

if(NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB ALIAS ZLIB::ZLIBSTATIC)
endif()

set(ZLIB_FOUND TRUE)
set(ZLIB_LIBRARIES ZLIB::ZLIB)
