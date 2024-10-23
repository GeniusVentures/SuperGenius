FUNCTION(ADD_COMPILE_FLAG value)
    MESSAGE(STATUS "Building with ${value}")

    FOREACH(variable CMAKE_C_FLAGS CMAKE_CXX_FLAGS)
        SET(${variable} "${${variable}} ${value}" PARENT_SCOPE)
    ENDFOREACH(variable)
ENDFUNCTION()

FUNCTION(ADD_CXX_FLAG value)
    MESSAGE(STATUS "Building with ${value}")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${value}" PARENT_SCOPE)
ENDFUNCTION()

FUNCTION(ADD_DEBUG_COMPILE_FLAG value)
    IF("${CMAKE_BUILD_TYPE}" MATCHES "Debug")
        MESSAGE(STATUS "Building with ${value}")
    ENDIF()

    FOREACH(variable CMAKE_C_FLAGS_DEBUG CMAKE_CXX_FLAGS_DEBUG)
        SET(${variable} "${${variable}} ${value}" PARENT_SCOPE)
    ENDFOREACH(variable)
ENDFUNCTION()

FUNCTION(ADD_NONDEBUG_COMPILE_FLAG value)
    IF(NOT "${CMAKE_BUILD_TYPE}" MATCHES "Debug")
        MESSAGE(STATUS "Building with ${value}")
    ENDIF()

    FOREACH(variable CMAKE_C_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_C_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_MINSIZEREL)
        SET(${variable} "${${variable}} ${value}" PARENT_SCOPE)
    ENDFOREACH(variable)
ENDFUNCTION()

FUNCTION(ADD_LINK_FLAG value)
    MESSAGE(STATUS "Linking with ${value}")

    FOREACH(variable CMAKE_EXE_LINKER_FLAGS)
        SET(${variable} "${${variable}} ${value}" PARENT_SCOPE)
    ENDFOREACH(variable)
ENDFUNCTION()

if(NOT CMAKE_BUILD_TYPE MATCHES "Debug")
    foreach(flags_var_to_scrub
        CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_RELWITHDEBINFO
        CMAKE_CXX_FLAGS_MINSIZEREL
        CMAKE_C_FLAGS_RELEASE
        CMAKE_C_FLAGS_RELWITHDEBINFO
        CMAKE_C_FLAGS_MINSIZEREL)
        string(REGEX REPLACE "(^| )[/-]D *NDEBUG($| )" " "
            "${flags_var_to_scrub}" "${${flags_var_to_scrub}}")
    endforeach()
endif()

IF(RUN_STATIC_ANALYZER)
    ADD_DEFINITIONS(/analyze)
ENDIF()