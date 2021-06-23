# TODO: Unable to link it for iOS
if (NOT ${CMAKE_SYSTEM_NAME} MATCHES "iOS")
add_executable(processing_app
    processing_app.cpp
    )


# Copy the required headers to install directory
include_directories(
    ${PROJECT_SOURCE_DIR}/src
    )

target_include_directories(processing_app PRIVATE ${GSL_INCLUDE_DIR})


target_link_libraries(processing_app
    processing_service
    Boost::program_options
    )
endif()
