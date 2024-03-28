#add_subdirectory(application)
set(MNN_INCLUDE_DIR "${_THIRDPARTY_BUILD_DIR}/MNN/include")
set(MNN_LIBRARY_DIR "${_THIRDPARTY_BUILD_DIR}/MNN/lib")
include_directories(${MNN_INCLUDE_DIR})
set(MNN_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../MNN/include")
FILE(GLOB MNN_LIBS "${MNN_LIBRARY_DIR}/*")


add_executable(mnn_chunkprocess multiPose.cpp)
if(APPLE)
set(XCFRAMEWORK_PATH "${_THIRDPARTY_BUILD_DIR}/moltenvk/build/lib/MoltenVK.xcframework")
target_link_libraries(mnn_chunkprocess PRIVATE 	
    "-framework CoreFoundation"
	"-framework CoreGraphics"
	"-framework CoreServices"
	"-framework IOKit"
	"-framework IOSurface"
	"-framework Metal"
	"-framework QuartzCore"
	"-framework AppKit"
    ${XCFRAMEWORK_PATH} 
	)
endif()
target_link_libraries(mnn_chunkprocess PRIVATE 	

	${MNN_LIBS})
if(WIN32)
	target_link_options(mnn_chunkprocess PRIVATE /WHOLEARCHIVE:${MNN_LIBS})
else()
	target_link_options(mnn_chunkprocess PRIVATE "-Wl,-force_load,${MNN_LIBS}")
endif()
include_directories(../include)
include_directories(./imageHelper)