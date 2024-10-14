add_library(json_secure_storage
    impl/json/JSONSecureStorage.cpp
)
target_link_libraries(json_secure_storage
    outcome
    rapidjson
)

supergenius_install(json_secure_storage)