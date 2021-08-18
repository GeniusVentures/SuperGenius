
# ------------------------------------------
# Set PROJECT_ROOT folder
get_filename_component(CURRENT_SOURCE_PARENT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY ABSOLUTE)
get_filename_component(PROJECT_ROOT "${CURRENT_SOURCE_PARENT}" DIRECTORY ABSOLUTE)

# Package config ######################################################################################
set (CPACK_PACKAGE_VERSION_MAJOR "21")
set (CPACK_PACKAGE_VERSION_MINOR "0")
set (CPACK_PACKAGE_VERSION_PATCH "0")
set (CPACK_PACKAGE_VERSION_PRE_RELEASE "12")
set (CPACK_PACKAGE_VENDOR "Genius Ventures")

set(CMAKE_INSTALL_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/SuperGenius)

include(GNUInstallDirs)
include (GenerateExportHeader)
include(CMakePackageConfigHelpers)
include(CheckCXXCompilerFlag)
include(${PROJECT_ROOT}/cmake/functions.cmake)
include(${PROJECT_ROOT}/cmake/install.cmake)
include(${PROJECT_ROOT}/cmake/definition.cmake)
include(${PROJECT_ROOT}/build/CompilationFlags.cmake)

if(EXISTS "${PROJECT_ROOT}/cmake/compile_option_by_platform/${CMAKE_SYSTEM_NAME}.cmake")
  print("add compile option: ${PROJECT_ROOT}/cmake/compile_option_by_platform/${CMAKE_SYSTEM_NAME}.cmake")
  include("${PROJECT_ROOT}/cmake/compile_option_by_platform/${CMAKE_SYSTEM_NAME}.cmake")
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${EXTRA_CXX_FLAGS}")
if (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
  set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
  set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MTd")

  if (CMAKE_BUILD_TYPE EQUAL Debug)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MTd")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MTd")
    set(MSVC_RUNTIME_LIBRARY_OPTION "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif (CMAKE_BUILD_TYPE EQUAL Debug)

  if (CMAKE_BUILD_TYPE EQUAL Release)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MT")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MT")
    set(MSVC_RUNTIME_LIBRARY_OPTION "MultiThreaded$<$<CONFIG:Realease>:Release>")
  endif (CMAKE_BUILD_TYPE EQUAL Release)

  print("CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
  set(CompilerFlags
          CMAKE_CXX_FLAGS
          CMAKE_CXX_FLAGS_DEBUG
          CMAKE_CXX_FLAGS_RELEASE
          CMAKE_C_FLAGS
          CMAKE_C_FLAGS_DEBUG
          CMAKE_C_FLAGS_RELEASE
          )
  foreach(CompilerFlag ${CompilerFlags})
    string(REPLACE "/MD" "/MT" ${CompilerFlag} "${${CompilerFlag}}")
  endforeach()
endif()

if (NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
  # https://cgold.readthedocs.io/en/latest/tutorials/toolchain/globals/cxx-standard.html#summary
  set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)
endif ()

# --------------------------------------------------------
# define third party directory
set(_THIRDPARTY_BUILD_DIR "${THIRDPARTY_DIR}")
if (NOT _THIRDPARTY_BUILD_DIR)
  message(FATAL_ERROR "_THIRDPARTY_BUILD_DIR not found")
endif()
print("THIRDPARTY BUILD DIR: ${_THIRDPARTY_BUILD_DIR}")

get_filename_component(_THIRDPARTY_DIR "${_THIRDPARTY_BUILD_DIR}" DIRECTORY ABSOLUTE)
if(NOT EXISTS "${_THIRDPARTY_DIR}/build")
  get_filename_component(_THIRDPARTY_DIR "${_THIRDPARTY_DIR}" DIRECTORY ABSOLUTE)
endif()
if(NOT EXISTS "${_THIRDPARTY_DIR}/build")
  message(FATAL_ERROR "Unable to find thirdparty source folder relative to build folder ${_THIRDPARTY_BUILD_DIR}")
endif()
print("THIRDPARTY SRC DIR: ${_THIRDPARTY_DIR}")
