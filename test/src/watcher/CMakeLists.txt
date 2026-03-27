addtest(messaging_watcher_test
    messaging_watcher_test.cpp
)

target_link_libraries(messaging_watcher_test
    evm_watcher_service
)

