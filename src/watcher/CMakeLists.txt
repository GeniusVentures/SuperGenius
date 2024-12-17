add_subdirectory(impl)

add_library(watcher_service
    messaging_watcher.cpp
    messaging_watcher.hpp
)

target_link_libraries(watcher_service
    PUBLIC
    rapidjson
    Boost::thread
)
