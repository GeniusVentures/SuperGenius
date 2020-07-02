cmake_minimum_required(VERSION 3.5.1)
# Project
project(SuperGenius C CXX)

include(cmake/functions.cmake)
include(cmake/print.cmake)

# BOOST library 
set(Boost_USE_STATIC_LIBS ON) 
set(Boost_USE_MULTITHREADED ON)  
set(Boost_USE_STATIC_RUNTIME ON) 
find_package(Boost REQUIRED COMPONENTS date_time filesystem random regex) # header only libraries must not be added here

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(blocklattice REQUIRED)

option(TESTING "Build tests" ON)

add_subdirectory(node)


if (TESTING)
  enable_testing()
  add_subdirectory(test)
endif ()