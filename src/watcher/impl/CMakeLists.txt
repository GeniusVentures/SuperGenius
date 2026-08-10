add_library(evm_watcher_service
    evm_messaging_watcher.hpp
    evm_messaging_watcher.cpp
    bridge_rpc_watcher.hpp
    bridge_rpc_watcher.cpp
    bridge_catchup_watcher.hpp
    bridge_catchup_watcher.cpp
)

target_link_libraries(evm_watcher_service
    PUBLIC
    watcher_service
    api_transport
    evmrelay
)

supergenius_install(evm_watcher_service)
