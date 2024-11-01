#include "evm_messaging_dapp.hpp"
#include <iostream>
#include <csignal>
#include <boost/thread.hpp>
#include <rapidjson/document.h>
#include "watcher/impl/evm_messaging_watcher.hpp"

std::atomic<bool> keepRunning(true);

void signalHandler(int signum) {
    keepRunning = false;
    std::cout << "\nInterrupt signal (" << signum << ") received. Stopping watcher..." << std::endl;
}

void customMessageHandler(const std::string &message) {
    std::cout << "Custom Message Handler: " << message << std::endl;
}

EvmMessagingDapp::EvmMessagingDapp(const std::vector<rapidjson::Document> &configs, const std::vector<std::string> &contractAddresses,
                                   const std::vector<std::vector<sgns::evmwatcher::EvmMessagingWatcher::TopicFilter>> &topicFilters) {
    if (configs.size() != contractAddresses.size() || configs.size() != topicFilters.size()) {
        throw std::runtime_error("The number of configurations, contract addresses, and topic filter sets must match");
    }

    // Loop through each config and create an EvmMessagingWatcher for each configuration, contract address, and set of topic filters
    for (size_t i = 0; i < configs.size(); ++i) {
        auto evmWatcher = std::make_shared<sgns::evmwatcher::EvmMessagingWatcher>(configs[i], customMessageHandler, contractAddresses[i], topicFilters[i]);
        sgns::watcher::MessagingWatcher::addWatcher(evmWatcher);
    }
}

void EvmMessagingDapp::start() {
    // Start all watchers
    sgns::watcher::MessagingWatcher::startAll();
    std::cout << "EvmMessagingDapp is running. Press Ctrl+C to stop..." << std::endl;

    while (keepRunning) {
        boost::this_thread::sleep_for(boost::chrono::seconds(1));
    }

    stop();
}

void EvmMessagingDapp::stop() {
    // Stop all watchers
    sgns::watcher::MessagingWatcher::stopAll();
}

int main() {
    signal(SIGINT, signalHandler);

    // Load the configuration directly as a JSON string representing an array of configs
    const char* json = R"([
        {
            "rpc_url": "http://localhost:8545",
            "chain_id": "1",
            "chain_name": "Ethereum Mainnet",
            "ws_url": "ws://localhost:8546"
        }
    ])";

    // Parse the JSON string to a rapidjson document
    rapidjson::Document configArray;
    configArray.Parse(json);

    // Define topics to watch for each configuration
    std::vector<std::vector<sgns::evmwatcher::EvmMessagingWatcher::TopicFilter>> const topicFilters = {
        {
            {"BridgeSourceBurned(address,uint256,uint256,uint256,uint256)"}
        }
    };

    // The contract addresses that we want to watch
    std::vector<std::string> contractAddresses = {
        "0x1234567890abcdef1234567890abcdef12345678"
    };

    // Convert the JSON array to a vector of rapidjson::Document configurations
    std::vector<rapidjson::Document> configs;
    if (configArray.IsArray()) {
        for (rapidjson::SizeType i = 0; i < configArray.Size(); ++i) {
            rapidjson::Document config;
            config.CopyFrom(configArray[i], config.GetAllocator());
            configs.push_back(std::move(config));
        }
    }

    // Ensure that the number of configurations matches the number of contract addresses
    if (configs.size() != contractAddresses.size()) {
        throw std::runtime_error("The number of configurations must match the number of contract addresses");
    }

    // Create and start the EvmMessagingDapp with the array of configurations and contract addresses
    for (size_t i = 0; i < configs.size(); ++i) {
        EvmMessagingDapp dapp(configs, contractAddresses, topicFilters);
        dapp.start();
    }

    return 0;
}
