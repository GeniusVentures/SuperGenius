#include "evm_messaging_watcher.hpp"
#include <iostream>

namespace sgns::evmwatcher {

    EvmMessagingWatcher::EvmMessagingWatcher(const rapidjson::Document &config, MessageCallback callback,
                                             const std::string &contract_address, const std::vector<TopicFilter> &topic_filters)
        : MessagingWatcher(std::move(callback)),
          contract_address(contract_address),
          topic_filters(topic_filters) {
            parseConfig(config);  // Explicitly call parseConfig to parse the configuration passed to the constructor
    }


    // Getters and setters for contract address
    std::string EvmMessagingWatcher::getContractAddress() const {
        return contract_address;
    }

    void EvmMessagingWatcher::setContractAddress(const std::string &contract_address) {
        this->contract_address = contract_address;
    }

    // Getters and setters for topic filters
    std::vector<EvmMessagingWatcher::TopicFilter> EvmMessagingWatcher::getTopicFilters() const {
        return topic_filters;
    }

    void EvmMessagingWatcher::setTopicFilters(const std::vector<TopicFilter> &topic_filters) {
        this->topic_filters = topic_filters;
    }

    void EvmMessagingWatcher::setupWebSocketListener(const ChainConfig &chainConfig) {
        std::cout << "Setting up WebSocket client for chain: " << chainConfig.chain_name
                  << " with contract filter: " << contract_address << std::endl;

        // Create an instance of WsClientImpl to connect to the WebSocket server
        ws_client = std::make_shared<sgns::api::WsClientImpl>(ws_context, chainConfig.ws_url);

        // Prepare the subscription request JSON dynamically, based on topic_filters
        std::string subscribe_request = R"({
            "jsonrpc": "2.0",
            "id": 1,
            "method": "eth_subscribe",
            "params": [
                "logs",
                {
                    "address": ")" + contract_address + R"(",
                    "topics": [
        )";

        // Add each topic filter to the JSON request
        for (size_t i = 0; i < topic_filters.size(); ++i) {
            subscribe_request += "\"" + topic_filters[i].topic_hash + "\"";
            if (i != topic_filters.size() - 1) {
                subscribe_request += ", ";
            }
        }

        subscribe_request += R"(]
                }
            ]
        })";

        // Log the request for debugging purposes
        std::cout << "Subscription Request: " << subscribe_request << std::endl;

        // Set up the WebSocket message handler
        ws_client->setMessageHandler([this](const std::string &message) {
            rapidjson::Document doc;
            doc.Parse(message.c_str());
            if (doc.HasMember("method") && std::string(doc["method"].GetString()) == "eth_subscription" &&
                doc.HasMember("params") && doc["params"].IsObject()) {
                if (messageCallback) {
                    messageCallback(message);  // Only call for actual event messages
                }
            }
        });
        // Start the WebSocket client and send the subscription request
        ws_client->start();
        ws_client->send(subscribe_request);

        std::cout << "Subscription request sent for contract: " << contract_address << std::endl;
    }

    void EvmMessagingWatcher::parseConfig(const rapidjson::Document &config) {
        if (!config.IsObject()) {
            throw std::runtime_error("Config must be a JSON object representing chain configuration");
        }

        if (!config.HasMember("rpc_url") || !config["rpc_url"].IsString()) {
            throw std::runtime_error("Invalid or missing rpc_url in chain configuration");
        }
        if (!config.HasMember("chain_id") || !config["chain_id"].IsString()) {
            throw std::runtime_error("Invalid or missing chain_id in chain configuration");
        }
        if (!config.HasMember("chain_name") || !config["chain_name"].IsString()) {
            throw std::runtime_error("Invalid or missing chain_name in chain configuration");
        }
        if (!config.HasMember("ws_url") || !config["ws_url"].IsString()) {
            throw std::runtime_error("Invalid or missing ws_url in chain configuration");
        }

        chain.rpc_url = config["rpc_url"].GetString();
        chain.chain_id = config["chain_id"].GetString();
        chain.chain_name = config["chain_name"].GetString();
        chain.ws_url = config["ws_url"].GetString();
    }

    void EvmMessagingWatcher::startWatching() {
        // Start WebSocket clients or other services specific to this watcher
        if (ws_client) {
            ws_client->start();  // Start the WebSocket clients
        }

        // Call base class startWatching to handle thread starting
        MessagingWatcher::startWatching();
    }

    void EvmMessagingWatcher::stopWatching() {
        // Stop WebSocket clients or other services specific to this watcher
        if (ws_client)  {
            ws_client->stop();  // Properly stop all WebSocket clients
        }

        // Call base class stopWatching to handle thread stopping
        MessagingWatcher::stopWatching();
    }

    EvmMessagingWatcher::ChainConfig EvmMessagingWatcher::getChain() const {
        return chain;
    }


}  // namespace sgns::evmwatcher
