add_executable(processing_dapp
    processing_dapp.cpp
    )


# Copy the required headers to install directory
include_directories(
    ${PROJECT_SOURCE_DIR}/src
    )

target_include_directories(processing_dapp PRIVATE ${GSL_INCLUDE_DIR})


target_link_libraries(processing_dapp
    processing_service
    crdt_globaldb
    logger
    Boost::program_options    
    )

add_executable(processing_dapp_processor
    processing_dapp_processor.cpp
    )


# Copy the required headers to install directory
include_directories(
    ${PROJECT_SOURCE_DIR}/src
    )

target_include_directories(processing_dapp PRIVATE ${GSL_INCLUDE_DIR})


target_link_libraries(processing_dapp_processor
    processing_service
    crdt_globaldb
    logger
    Boost::program_options    
    )


if(FORCE_MULTILE)
  set_target_properties(processing_dapp_processor PROPERTIES LINK_FLAGS "${MULTIPLE_OPTION}")
endif()
