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

EvmMessagingDapp::EvmMessagingDapp(const rapidjson::Document &config, const std::string &contractAddress,
                                   const std::vector<sgns::evmwatcher::EvmMessagingWatcher::TopicFilter> &topicFilters) {
    evmWatcher = std::make_unique<sgns::evmwatcher::EvmMessagingWatcher>(config, customMessageHandler, contractAddress, topicFilters);
}

void EvmMessagingDapp::start() {
    evmWatcher->startWatching();
    std::cout << "EvmMessagingDapp is running. Press Ctrl+C to stop..." << std::endl;

    while (keepRunning) {
        boost::this_thread::sleep_for(boost::chrono::seconds(1));
    }

    stop();
}

void EvmMessagingDapp::stop() {
    evmWatcher->stopWatching();
    std::cout << "Watcher stopped." << std::endl;
}

int main() {
    signal(SIGINT, signalHandler);

    // Load the configuration directly as a JSON string
    const char* json = R"([
        {
            "rpc_url": "http://localhost:8545",
            "chain_id": "1",
            "chain_name": "Ethereum Mainnet",
            "ws_url": "ws://localhost:8546"
        }
    ])";

    rapidjson::Document config;
    config.Parse(json);

    // Define topics to watch
    std::vector<sgns::evmwatcher::EvmMessagingWatcher::TopicFilter> topicFilters = {
            {"BridgeSourceBurned(address,uint256,uint256,uint256,uint256)"}
    };

    // The contract address that we want to watch
    std::string contractAddress = "0x1234567890abcdef1234567890abcdef12345678";

    // Create and start the EvmMessagingDapp
    EvmMessagingDapp dapp(config, contractAddress, topicFilters);
    dapp.start();

    return 0;
}
