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

#Since we link directly to MNN here we need to wholearchive the lib path, this is fine
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    get_target_property(MNN_LIB_PATH MNN::MNN IMPORTED_LOCATION_DEBUG)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    get_target_property(MNN_LIB_PATH MNN::MNN IMPORTED_LOCATION_RELEASE)
endif()

#Wholearchive MNN
if(WIN32)
    target_link_options(mnn_chunkprocess PUBLIC /WHOLEARCHIVE:${MNN_LIB_PATH})
elseif(APPLE)
    target_link_options(mnn_chunkprocess PUBLIC -force_load ${MNN_LIB_PATH})
else()
    target_link_options(mnn_chunkprocess PUBLIC
        "-Wl,--whole-archive"
        ${MNN_LIB_PATH}
        "-Wl,--no-whole-archive"
    )
endif()
include_directories(../include)
include_directories(./imageHelper)
