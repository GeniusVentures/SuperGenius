cmake_minimum_required(VERSION 3.12)
set(
    CMAKE_TOOLCHAIN_FILE
    "${CMAKE_SOURCE_DIR}/cmake/toolchain/cxx17.cmake"
    CACHE
    FILEPATH
    "Default toolchain"
)
# Project
project(SuperGenius C CXX)

if (NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
  # https://cgold.readthedocs.io/en/latest/tutorials/toolchain/globals/cxx-standard.html#summary
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)
endif ()

include(cmake/functions.cmake)
include(cmake/print.cmake)
print("C flags: ${CMAKE_C_FLAGS}")
print("CXX flags: ${CMAKE_CXX_FLAGS}")
print("C Debug flags: ${CMAKE_C_FLAGS_DEBUG}")
print("CXX Debug flags: ${CMAKE_CXX_FLAGS_DEBUG}")
print("C Release flags: ${CMAKE_C_FLAGS_RELEASE}")
print("CXX Release flags: ${CMAKE_CXX_FLAGS_RELEASE}")
# BOOST library 
set(Boost_USE_STATIC_LIBS ON) 
set(Boost_USE_MULTITHREADED ON)  
set(Boost_USE_STATIC_RUNTIME ON) 
find_package(Boost REQUIRED COMPONENTS date_time filesystem random regex system) # header only libraries must not be added here

print("${blocklattice_INCLUDE_DIR}")

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
)
find_package(blocklattice CONFIG REQUIRED PATHS ${blocklattice_DIR})
option(TESTING "Build tests" ON)

add_subdirectory(node)


if (TESTING)
  enable_testing()
  add_subdirectory(test)
endif ()