#include "messaging_watcher.hpp"
#include <boost/thread.hpp>
#include <iostream>

namespace sgns::watcher {

    // Static vector to hold all watcher instances
    std::vector<std::shared_ptr<MessagingWatcher>> MessagingWatcher::m_watchers;

    MessagingWatcher::MessagingWatcher(MessageCallback callback)
        : messageCallback(std::move(callback)), running(false) {
    }

    void MessagingWatcher::startWatching() {
        std::lock_guard<std::mutex> lock(running_mutex);
        if (running) return;

        running = true;
        watcherThread = boost::thread(&MessagingWatcher::watch, this);
    }

    void MessagingWatcher::stopWatching() {
        {
            std::lock_guard<std::mutex> lock(running_mutex);
            running = false;
        }

        if (watcherThread.joinable()) {
            watcherThread.join();
        }
    }

    void MessagingWatcher::addWatcher(const std::shared_ptr<MessagingWatcher> &newWatcher) {
        m_watchers.push_back(newWatcher);
    }

    void MessagingWatcher::startAll() {
        for (const auto &watcher : m_watchers) {
            watcher->startWatching();
        }
    }

    void MessagingWatcher::stopAll() {
        for (const auto &watcher : m_watchers) {
            watcher->stopWatching();
        }
    }

    void MessagingWatcher::watch() {
        while (running) {
            // Placeholder: This function should be implemented by derived classes
            // Here, it will simply sleep for 1 second, but derived classes should provide a real implementation.
            boost::this_thread::sleep_for(boost::chrono::seconds(1));
        }
    }

    bool MessagingWatcher::isRunning() const {
        std::lock_guard<std::mutex> const lock(running_mutex);
        return running;
    }


}  // namespace sgns::watcher
