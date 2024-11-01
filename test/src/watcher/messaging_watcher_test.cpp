#include <gtest/gtest.h>
#include "watcher/impl/evm_messaging_watcher.hpp"
#include "watcher/messaging_watcher.hpp"
#include <rapidjson/document.h>
#include <memory>

using namespace sgns::watcher;
using namespace sgns::evmwatcher;

TEST(MessagingWatcherTest, EvmMessagingWatcherInitialization) {
    // Define JSON configuration for the test
    const char* json = R"({
        "rpc_url": "http://localhost:8545",
        "chain_id": "1",
        "chain_name": "Ethereum Mainnet",
        "ws_url": "ws://localhost:8546"
    })";

    rapidjson::Document config;
    config.Parse(json);

    // Define a contract address
    std::string contractAddress = "0x1234567890abcdef1234567890abcdef12345678";

    // Define topics to watch
    std::vector<EvmMessagingWatcher::TopicFilter> topicFilters = {
        {"BridgeSourceBurned(address,uint256,uint256,uint256,uint256)"}
    };

    // Create the EvmMessagingWatcher
    auto evmWatcher = std::make_shared<EvmMessagingWatcher>(config, nullptr, contractAddress, topicFilters);

    // Add watcher to the centralized watcher list
    MessagingWatcher::addWatcher(evmWatcher);

    auto chainConfig = evmWatcher->getChain();
    // Check that the watcher was properly initialized
    EXPECT_EQ(chainConfig.chain_name, "Ethereum Mainnet");
    EXPECT_EQ(chainConfig.chain_id, "1");
    EXPECT_EQ(chainConfig.rpc_url, "http://localhost:8545");
    EXPECT_EQ(chainConfig.ws_url, "ws://localhost:8546");
}

TEST(MessagingWatcherTest, StartAndStopWatching) {
    // Define JSON configuration for the test
    const char* json = R"({
        "rpc_url": "http://localhost:8545",
        "chain_id": "1",
        "chain_name": "Ethereum Mainnet",
        "ws_url": "ws://localhost:8546"
    })";

    rapidjson::Document config;
    config.Parse(json);

    // Define a contract address
    std::string contractAddress = "0x1234567890abcdef1234567890abcdef12345678";

    // Define topics to watch
    std::vector<EvmMessagingWatcher::TopicFilter> topicFilters = {
        {"BridgeSourceBurned(address,uint256,uint256,uint256,uint256)"}
    };

    // Create the EvmMessagingWatcher
    auto evmWatcher = std::make_shared<EvmMessagingWatcher>(config, nullptr, contractAddress, topicFilters);

    // Add watcher to the centralized watcher list
    MessagingWatcher::addWatcher(evmWatcher);

    // Start watching
    MessagingWatcher::startAll();

    // Validate watcher running state
    EXPECT_TRUE(evmWatcher->isRunning());

    // Stop watching
    MessagingWatcher::stopAll();

    // Validate watcher stopped state
    EXPECT_FALSE(evmWatcher->isRunning());
}

TEST(MessagingWatcherTest, MultipleWatchersManagement) {
    // Define multiple JSON configurations for the test
    const char* json1 = R"({
        "rpc_url": "http://localhost:8545",
        "chain_id": "1",
        "chain_name": "Ethereum Mainnet",
        "ws_url": "ws://localhost:8546"
    })";

    const char* json2 = R"({
        "rpc_url": "http://localhost:8545",
        "chain_id": "3",
        "chain_name": "Ropsten Testnet",
        "ws_url": "ws://localhost:8547"
    })";

    rapidjson::Document config1, config2;
    config1.Parse(json1);
    config2.Parse(json2);

    // Define contract addresses for both configurations
    std::string contractAddress1 = "0x1234567890abcdef1234567890abcdef12345678";
    std::string contractAddress2 = "0xabcdefabcdefabcdefabcdefabcdefabcdefabcdef";

    // Define topics to watch for both configurations
    std::vector<EvmMessagingWatcher::TopicFilter> topicFilters1 = {
        {"BridgeSourceBurned(address,uint256,uint256,uint256,uint256)"}
    };
    std::vector<EvmMessagingWatcher::TopicFilter> topicFilters2 = {
        {"AnotherEvent(address,uint256)"}
    };

    // Create the EvmMessagingWatcher instances
    auto evmWatcher1 = std::make_shared<EvmMessagingWatcher>(config1, nullptr, contractAddress1, topicFilters1);
    auto evmWatcher2 = std::make_shared<EvmMessagingWatcher>(config2, nullptr, contractAddress2, topicFilters2);

    // Add watchers to the centralized watcher list
    MessagingWatcher::addWatcher(evmWatcher1);
    MessagingWatcher::addWatcher(evmWatcher2);

    // Start watching all
    MessagingWatcher::startAll();

    // Validate both watchers running state
    EXPECT_TRUE(evmWatcher1->isRunning());
    EXPECT_TRUE(evmWatcher2->isRunning());

    // Stop watching all
    MessagingWatcher::stopAll();

    // Validate both watchers stopped state
    EXPECT_FALSE(evmWatcher1->isRunning());
    EXPECT_FALSE(evmWatcher2->isRunning());
}

