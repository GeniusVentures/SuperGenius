add_library(json_secure_storage
    impl/json/JSONSecureStorage.cpp
)

target_link_libraries(json_secure_storage
    PUBLIC
    Boost::filesystem
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
    find_package(PkgConfig)
    pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1>=0.18.4)

    # libsecret >= 0.19 provides secret_password_lookup/store_binary_sync, which
    # impl/Linux.cpp uses; older releases (0.18.x, e.g. AlmaLinux 8's 0.18.6)
    # only have the string API, provided by impl/LinuxCompat.cpp. Serialized
    # JSON carries no NUL bytes, so both implementations are equivalent.
    if(LIBSECRET_VERSION VERSION_GREATER_EQUAL "0.19")
        set(SECURE_STORAGE_IMPL impl/Linux.cpp)
    else()
        set(SECURE_STORAGE_IMPL impl/LinuxCompat.cpp)
        message(STATUS "libsecret ${LIBSECRET_VERSION} < 0.19: using string-API secure storage (LinuxCompat)")
    endif()
    target_sources(secure_storage PRIVATE ${SECURE_STORAGE_IMPL})

    target_link_libraries(secure_storage PUBLIC PkgConfig::LIBSECRET)
elseif(WIN32)
    target_sources(secure_storage PRIVATE impl/Windows.cpp)
elseif(APPLE)
    target_sources(secure_storage PRIVATE impl/Apple.cpp)
    target_link_libraries(secure_storage PRIVATE ${SECURITY_FRAMEWORK} ${CORE_FOUNDATION})
endif()

supergenius_install(secure_storage)
