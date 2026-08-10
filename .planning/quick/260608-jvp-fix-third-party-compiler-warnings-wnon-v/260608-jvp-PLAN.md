# Quick Task 260608-jvp: Fix third-party compiler warnings (-Wnon-virtual-dtor, -Wmissing-field-initializers)

**Date:** 2026-06-08
**Status:** Ready for execution

## Goal

Suppress two classes of compiler warnings from third-party headers by marking their include directories as `SYSTEM` in CMake.

## Root Cause

Third-party include directories are added via `include_directories()` without the `SYSTEM` keyword, causing the compiler to emit warnings from code the project doesn't control.

### Warning 1: `-Wnon-virtual-dtor` from Boost Beast zlib
- Triggered by: `evmrelay/src/eth/rpc_http_transport.cpp` → `#include <boost/beast.hpp>`
- Source header: `boost/beast/zlib/impl/error.ipp:49` (class `error_codes` inherits `error_category` with virtual functions but non-virtual destructor)

### Warning 2: `-Wmissing-field-initializers` from STB
- Triggered by: `example/mnn_chunkprocess/multiPose.cpp` → `#include "stb_image_write.h"`
- Source header: `stb_image_write.h:789` (`stbi__write_context s = { 0 }` missing `context` field initializer)

## Tasks

### Task 1: Mark STB include as SYSTEM
- **File:** `build/CommonBuildParameters.cmake`
- **Action:** Change line 123 from `include_directories(${_THIRDPARTY_BUILD_DIR}/stb/include)` to `include_directories(SYSTEM ${_THIRDPARTY_BUILD_DIR}/stb/include)`
- **Verify:** Rebuild example; `-Wmissing-field-initializers` warnings from stb headers should be gone

### Task 2: Mark Boost include as SYSTEM in CommonBuildParameters
- **File:** `build/CommonBuildParameters.cmake`
- **Action:** Change line 221 from `include_directories(${Boost_INCLUDE_DIRS})` to `include_directories(SYSTEM ${Boost_INCLUDE_DIRS})`
- **Verify:** Rebuild evmrelay; `-Wnon-virtual-dtor` warnings from Boost Beast zlib should be gone

### Task 3: Mark Boost include as SYSTEM in evmrelay CMakeLists
- **File:** `evmrelay/CMakeLists.txt`
- **Action:** Change line 45 from `include_directories(${Boost_INCLUDE_DIRS})` to `include_directories(SYSTEM ${Boost_INCLUDE_DIRS})`
- **Verify:** Standalone evmrelay build; `-Wnon-virtual-dtor` warnings from Boost Beast zlib should be gone

## must_haves
- No warnings from third-party Boost/STB headers when building SuperGenius and evmrelay
- Existing functionality unchanged (SYSTEM only affects warning emission, not include resolution)
