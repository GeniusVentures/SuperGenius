# CMakeLists.txt for the watcher directory (all files are included by default in the directory)

add_library(evm_watcher_service
        evm_messaging_watcher.hpp
        evm_messaging_watcher.cpp
)

target_link_libraries(evm_watcher_service
        watcher_service
        api_transport
)
