add_library(json_secure_storage
    impl/json/JSONSecureStorage.cpp
)

add_library(secure_storage STATIC ISecureStorage.hpp)
target_link_libraries(secure_storage PUBLIC outcome rapidjson)

if(LINUX)
    target_sources(secure_storage PUBLIC impl/Linux.cpp)

    find_package(PkgConfig)
    pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1>=0.18.4)
    target_link_libraries(secure_storage PUBLIC PkgConfig::LIBSECRET)
elseif(WIN32)
    target_sources(secure_storage PUBLIC impl/Windows.cpp)
elseif(APPLE)
    target_sources(secure_storage PUBLIC impl/OSX.cpp)
    target_link_libraries(secure_storage PRIVATE ${SECURITY_FRAMEWORK} ${CORE_FOUNDATION})
endif()

target_link_libraries(json_secure_storage
    PUBLIC
    outcome
    PRIVATE
    rapidjson
)

supergenius_install(json_secure_storage)
