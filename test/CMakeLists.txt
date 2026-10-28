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

# TEST-02 SC4 (v1.1 Phase 12 plan 12-03 Task 2): the tier-label static
# audit, registered INSIDE the fail-closed Tier-1 lane it polices (the
# thirdparty/sgns-blockstore tier1_audit_check.cmake precedent).
add_test(NAME tier1_static_audit
    COMMAND ${CMAKE_COMMAND} -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/.. -P
            ${CMAKE_CURRENT_SOURCE_DIR}/tier1_audit_check.cmake)
set_tests_properties(tier1_static_audit PROPERTIES LABELS "Tier1")