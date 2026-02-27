# Android AAR build for KeyStoreHelper
cmake_minimum_required(VERSION 3.22)

# Check if Gradle wrapper exists, if not, set it up
set(GRADLEW_PATH "${CMAKE_CURRENT_SOURCE_DIR}/gradlew")
if(NOT EXISTS "${GRADLEW_PATH}")
    message(STATUS "Gradle wrapper not found, running setup script...")
    execute_process(
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/setup-gradle-wrapper.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        RESULT_VARIABLE SETUP_RESULT
    )
    if(NOT SETUP_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to set up Gradle wrapper. Please check your internet connection.")
    endif()
    message(STATUS "Gradle wrapper setup complete")
endif()

# Custom target to build the AAR using Gradle
add_custom_target(build_android_aar ALL
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/gradlew :library:assembleRelease
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building securestorage AAR with Gradle"
    BYPRODUCTS ${CMAKE_CURRENT_SOURCE_DIR}/library/build/outputs/aar/library-release.aar
)

# Install the AAR to the installation directory
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/library/build/outputs/aar/library-release.aar
    DESTINATION lib/android
    RENAME securestorage-release.aar
    OPTIONAL
)
