#add_subdirectory(application)
set(MNN_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/MNN/include")
set(MNN_LIBRARY_DIR "${_THIRDPARTY_BUILD_DIR}/MNN/lib")
include_directories(${MNN_INCLUDE_DIR})
set(MNN_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../MNN/include")
FILE(GLOB MNN_LIBS "${MNN_LIBRARY_DIR}/*")


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

target_link_libraries(mnn_chunkprocess PRIVATE

    ${MNN_LIBS}
    Vulkan::Vulkan
)
if(WIN32)
    target_link_options(mnn_chunkprocess PUBLIC /WHOLEARCHIVE:${MNN_LIBS})
elseif(APPLE)
    target_link_options(mnn_chunkprocess PUBLIC -force_load ${MNN_LIBS})
else()
    target_link_options(mnn_chunkprocess PUBLIC "-Wl,--whole-archive" ${MNN_LIBS} "-Wl,--no-whole-archive")
endif()
include_directories(../include)
include_directories(./imageHelper)
