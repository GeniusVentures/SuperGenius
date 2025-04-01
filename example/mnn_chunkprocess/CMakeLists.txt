#add_subdirectory(application)
add_executable(mnn_chunkprocess multiPose.cpp)

if(APPLE)
    # Check if we are building for iOS
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(PLATFORM_SPECIFIC_FRAMEWORKS
            "-framework CoreFoundation"
            "-framework CoreGraphics"
            "-framework CoreServices"
            "-framework IOKit"
            "-framework IOSurface"
            "-framework Metal"
            "-framework QuartzCore"
        )
    else()
        # Assume macOS
        set(PLATFORM_SPECIFIC_FRAMEWORKS
            "-framework CoreFoundation"
            "-framework CoreGraphics"
            "-framework CoreServices"
            "-framework IOKit"
            "-framework IOSurface"
            "-framework Metal"
            "-framework QuartzCore"
            "-framework AppKit"
        )
    endif()

    set(XCFRAMEWORK_PATH "${_THIRDPARTY_BUILD_DIR}/moltenvk/build/lib/MoltenVK.xcframework")
    target_link_libraries(mnn_chunkprocess PRIVATE
        ${PLATFORM_SPECIFIC_FRAMEWORKS}
#        ${XCFRAMEWORK_PATH}
    )
endif()
if(ANDROID)
    find_library( log-lib log )
    target_link_libraries(mnn_chunkprocess PRIVATE
    ${log-lib}
)
endif()

target_link_libraries(mnn_chunkprocess PUBLIC
	MNN::MNN
    Vulkan::Vulkan
)


include_directories(../include)
include_directories(./imageHelper)
