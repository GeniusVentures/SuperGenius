add_library(json_secure_storage
    impl/json/JSONSecureStorage.cpp
)

target_link_libraries(json_secure_storage
    PUBLIC
    outcome
    rapidjson
)

supergenius_install(json_secure_storage)

add_library(secure_storage STATIC impl/JSONBackend.cpp)
target_link_libraries(secure_storage PUBLIC outcome rapidjson)

if(ANDROID)
    target_sources(secure_storage PRIVATE impl/Android.cpp)
    target_link_libraries(secure_storage
        PUBLIC
        android::android
        PRIVATE
        android::log
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(secure_storage PRIVATE impl/Linux.cpp)

    find_package(PkgConfig)
    pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1>=0.18.4)
    target_link_libraries(secure_storage PUBLIC PkgConfig::LIBSECRET)
elseif(WIN32)
    target_sources(secure_storage PRIVATE impl/Windows.cpp)
elseif(APPLE)
    target_sources(secure_storage PRIVATE impl/Apple.cpp)
    target_link_libraries(secure_storage PRIVATE ${SECURITY_FRAMEWORK} ${CORE_FOUNDATION})
endif()

supergenius_install(secure_storage)
