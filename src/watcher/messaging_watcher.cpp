#include "messaging_watcher.hpp"

namespace sgns::watcher {
    MessagingWatcher::MessagingWatcher(const rapidjson::Document &config, MessageCallback callback)
            : messageCallback(callback), running(false) {
        parseConfig(config);
    }

    void MessagingWatcher::parseConfig(const rapidjson::Document &config) {
        if (!config.IsArray()) {
            throw std::runtime_error("Config must be an array of chain configurations");
        }

        for (rapidjson::SizeType i = 0; i < config.Size(); i++) {
            const auto &chain = config[i];
            ChainConfig chainConfig;
            chainConfig.rpc_url = chain["rpc_url"].GetString();
            chainConfig.chain_id = chain["chain_id"].GetString();
            chainConfig.chain_name = chain["chain_name"].GetString();
            chainConfig.ws_url = chain["ws_url"].GetString();
            chains.push_back(chainConfig);
        }
    }

    std::vector <MessagingWatcher::ChainConfig> MessagingWatcher::getChains() const {
        return chains;
    }

    void MessagingWatcher::startWatching() {
        if (running) return; // Already running

        running = true;
        watcherThread = boost::thread(&MessagingWatcher::watch, this);
    }

    void MessagingWatcher::stopWatching() {
        running = false;
        if (watcherThread.joinable()) {
            watcherThread.join();
        }
    }

    void MessagingWatcher::watch() {
        for (const auto &chain: chains) {
            setupHttpListener(chain);
            setupWebSocketListener(chain);
        }

        while (running) {
            boost::this_thread::sleep_for(boost::chrono::seconds(1));
        }
    }

    void MessagingWatcher::setupHttpListener(const ChainConfig &chainConfig) {
        std::cout << "Started HTTP listener for chain: " << chainConfig.chain_name << std::endl;
    }

    void MessagingWatcher::setupWebSocketListener(const ChainConfig &chainConfig) {
        std::cout << "Started WebSocket listener for chain: " << chainConfig.chain_name << std::endl;

        if (messageCallback) {
            messageCallback("Message received from " + chainConfig.chain_name);
        }
    }
}
