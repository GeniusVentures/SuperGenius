#ifndef MESSAGING_WATCHER_HPP
#define MESSAGING_WATCHER_HPP

#include <rapidjson/document.h>
#include <boost/thread.hpp>
#include <functional>
#include <stdlib.h>
#include <vector>
#include <string>
#include <iostream>

namespace sgns::watcher {
    class MessagingWatcher {
    public:
        using MessageCallback = std::function<void(const std::string &)>;

        struct ChainConfig {
            std::string rpc_url;
            std::string chain_id;
            std::string chain_name;
            std::string ws_url;
        };

        MessagingWatcher(const rapidjson::Document &config, MessageCallback callback);

        void startWatching();

        void stopWatching();

        std::vector<ChainConfig> getChains() const; // Public getter for chains_

        MessageCallback messageCallback;
    private:
        void parseConfig(const rapidjson::Document &config);

        void watch();

        void setupHttpListener(const ChainConfig &chainConfig);

        void setupWebSocketListener(const ChainConfig &chainConfig);

        std::vector<ChainConfig> chains;
        boost::thread watcherThread;
        bool running;
    };

}
#endif // MESSAGING_WATCHER_HPP
