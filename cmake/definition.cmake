
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
IF(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "19.0") # VS2013 and older explicitly need /arch:sse2 set, VS2015 no longer has that option, but always enabled.
ADD_COMPILE_FLAG("/arch:sse2")
ENDIF()
ADD_COMPILE_FLAG("/wd4146") # Ignore warning "warning C4146: unary minus operator applied to unsigned type, result still unsigned", this pattern is used somewhat commonly in the code.
# 4267 and 4244 are conversion/truncation warnings. We might want to fix these but they are currently pervasive.
ADD_COMPILE_FLAG("/wd4267")
ADD_COMPILE_FLAG("/wd4244")
# 4722 warns that destructors never return, even with WASM_NORETURN.
ADD_COMPILE_FLAG("/wd4722")
ADD_COMPILE_FLAG("/WX-")
ADD_DEBUG_COMPILE_FLAG("/Od")
ADD_NONDEBUG_COMPILE_FLAG("/O2")
ADD_COMPILE_FLAG("/D_CRT_SECURE_NO_WARNINGS")
ADD_COMPILE_FLAG("/D_SCL_SECURE_NO_WARNINGS")
# Visual Studio 2018 15.8 implemented conformant support for std::aligned_storage, but the conformant support is only enabled when the following flag is passed, to avoid
# breaking backwards compatibility with code that relied on the non-conformant behavior (the old nonconformant behavior is not used with Binaryen)
ADD_COMPILE_FLAG("/D_ENABLE_EXTENDED_ALIGNED_STORAGE")
# Don't warn about using "strdup" as a reserved name.
ADD_COMPILE_FLAG("/D_CRT_NONSTDC_NO_DEPRECATE")

ADD_NONDEBUG_COMPILE_FLAG("/UNDEBUG") # Keep asserts.
# Also remove /D NDEBUG to avoid MSVC warnings about conflicting defines.
if( NOT CMAKE_BUILD_TYPE MATCHES "Debug" )
foreach (flags_var_to_scrub
    CMAKE_CXX_FLAGS_RELEASE
    CMAKE_CXX_FLAGS_RELWITHDEBINFO
    CMAKE_CXX_FLAGS_MINSIZEREL
    CMAKE_C_FLAGS_RELEASE
    CMAKE_C_FLAGS_RELWITHDEBINFO
    CMAKE_C_FLAGS_MINSIZEREL)
  string (REGEX REPLACE "(^| )[/-]D *NDEBUG($| )" " "
    "${flags_var_to_scrub}" "${${flags_var_to_scrub}}")
endforeach()
endif()

ADD_LINK_FLAG("/STACK:8388608")

IF(RUN_STATIC_ANALYZER)
ADD_DEFINITIONS(/analyze)
ENDIF()