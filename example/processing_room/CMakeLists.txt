add_executable(processing_app
    processing_app.cpp
    )


# Copy the required headers to install directory
include_directories(
    ${PROJECT_SOURCE_DIR}/src
    )

target_include_directories(processing_app PRIVATE ${GSL_INCLUDE_DIR})


target_link_libraries(processing_app
	SGProcessingProto
    processing_service
    logger
    Boost::program_options    
    )


if(FORCE_MULTILE)
  set_target_properties(processing_app PROPERTIES LINK_FLAGS "${MULTIPLE_OPTION}")
endif()
