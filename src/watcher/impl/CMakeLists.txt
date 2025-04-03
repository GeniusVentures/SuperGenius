add_library(evm_watcher_service
    evm_messaging_watcher.hpp
    evm_messaging_watcher.cpp
)

target_link_libraries(evm_watcher_service
    PUBLIC
    watcher_service
    api_transport
)
