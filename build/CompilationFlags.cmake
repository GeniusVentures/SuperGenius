if("${CMAKE_CXX_COMPILER_ID}" MATCHES "^(AppleClang|Clang|GNU)$")
    # enable those flags
    add_flag(-Wall)
    add_flag(-Wextra)
    add_flag(-Woverloaded-virtual) # warn if you overload (not override) a virtual function
    add_flag(-Wformat=2) # warn on security issues around functions that format output (ie printf)
    add_flag(-Wduplicated-cond) # (only in GCC >= 6.0) warn if if / else chain has duplicated conditions
    add_flag(-Wduplicated-branches) # (only in GCC >= 7.0) warn if if / else branches have duplicated code
    add_flag(-Wnull-dereference) # (only in GCC >= 6.0) warn if a null dereference is detected
    add_flag(-Wsign-compare)
    add_flag(-Wnon-virtual-dtor) # warn the user if a class with virtual functions has a non-virtual destructor. This helps catch hard to track down memory errors

    # disable those flags
    add_flag(-Wno-unused-command-line-argument) # clang: warning: argument unused during compilation: '--coverage' [-Wunused-command-line-argument]
    add_flag(-Wno-double-promotion) # (GCC >= 4.6, Clang >= 3.8) warn if float is implicit promoted to double
    add_flag(-Wno-gnu-zero-variadic-macro-arguments) # https://stackoverflow.com/questions/21266380/is-the-gnu-zero-variadic-macro-arguments-safe-to-ignore
	add_flag(-Wno-unused-result) #Every logger call generates this
    add_flag(-Wno-pessimizing-move) # Warning was irrelevant to situation
    add_flag(-Wno-macro-redefined)
endif()
