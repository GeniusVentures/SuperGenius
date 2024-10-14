#include "evm_messaging_watcher.hpp"
#include <iostream>

namespace sgns::evmwatcher {

    EvmMessagingWatcher::EvmMessagingWatcher(const rapidjson::Document &config, MessageCallback callback,
                                             const std::string &contract_address, const std::vector<TopicFilter> &topic_filters)
            : MessagingWatcher(config, callback), contract_address(contract_address), topic_filters(topic_filters) {}

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
            // Handle incoming WebSocket message
            std::cout << "WebSocket Message Received: " << message << std::endl;

            // Pass the message to the user-defined callback
            if (messageCallback) {
                messageCallback(message);
            }
        });

        // Start the WebSocket client and send the subscription request
        ws_client->start();
        ws_client->send(subscribe_request);

        std::cout << "Subscription request sent for contract: " << contract_address << std::endl;
    }

}  // namespace sgns::evmwatcher
