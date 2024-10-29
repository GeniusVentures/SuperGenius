# CMakeLists.txt for the watcher directory (all files are included by default in the directory)

add_subdirectory(impl)

add_library(watcher_service
        messaging_watcher.cpp
        messaging_watcher.hpp
)

target_link_libraries(watcher_service rapidjson Boost::thread
)
