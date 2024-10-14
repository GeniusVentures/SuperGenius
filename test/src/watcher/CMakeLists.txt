
addtest(messaging_watcher_test
        messaging_watcher_test.cpp
)

target_link_libraries(messaging_watcher_test
        watcher_service
        Boost::thread
        Boost::chrono
        blob
)

