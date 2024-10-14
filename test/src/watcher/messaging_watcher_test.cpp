#include <gtest/gtest.h>
#include "watcher/messaging_watcher.hpp"
#include <rapidjson/document.h>
#include <string>

// Custom callback for testing
static std::string received_message;
void testCallback(const std::string &message) {
    received_message = message; // Store the received message for validation
}

TEST(MessagingWatcherTest, ParseConfig) {
    const char* json = R"([
        {"rpc_url": "http://localhost:8545", "chain_id": "1", "chain_name": "Ethereum Mainnet", "ws_url": "ws://localhost:8546"},
        {"rpc_url": "http://localhost:8546", "chain_id": "56", "chain_name": "Binance Smart Chain", "ws_url": "ws://localhost:8547"}
    ])";

    rapidjson::Document config;
    config.Parse(json);

    // Create MessagingWatcher with test callback
    sgns::watcher::MessagingWatcher watcher(config, testCallback);

    // Validate that the chain configuration is parsed correctly
    EXPECT_EQ(watcher.getChains().size(), 2);
    EXPECT_EQ(watcher.getChains()[0].chain_name, "Ethereum Mainnet");
    EXPECT_EQ(watcher.getChains()[1].chain_name, "Binance Smart Chain");
    EXPECT_EQ(watcher.getChains()[0].rpc_url, "http://localhost:8545");
    EXPECT_EQ(watcher.getChains()[1].rpc_url, "http://localhost:8546");
}

TEST(MessagingWatcherTest, StartWatchingAndReceiveMessage) {
    const char* json = R"([
        {"rpc_url": "http://localhost:8545", "chain_id": "1", "chain_name": "Ethereum Mainnet", "ws_url": "ws://localhost:8546"}
    ])";

    rapidjson::Document config;
    config.Parse(json);

    // Create MessagingWatcher with test callback
    sgns::watcher::MessagingWatcher watcher(config, testCallback);

    // Start watching
    watcher.startWatching();

    // Simulate a message received
    const std::string dummy_message = "Test message from Ethereum Mainnet";
    if (watcher.messageCallback) {
        watcher.messageCallback(dummy_message);
    }

    // Validate that the callback was called with the correct message
    EXPECT_EQ(received_message, dummy_message);

    // Stop watching to clean up the thread
    watcher.stopWatching();
}
