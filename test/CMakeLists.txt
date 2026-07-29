include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/node
    )

add_subdirectory(src)
add_subdirectory(testutil)

option(SGNS_FUZZING "Build libFuzzer+ASan harnesses (Clang only)" OFF)
if(SGNS_FUZZING)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(STATUS "SGNS_FUZZING requires Clang (libFuzzer); current compiler is ${CMAKE_CXX_COMPILER_ID} — fuzz targets will likely fail to build.")
    endif()
    add_subdirectory(${CMAKE_SOURCE_DIR}/fuzz ${CMAKE_BINARY_DIR}/fuzz)
endif()
