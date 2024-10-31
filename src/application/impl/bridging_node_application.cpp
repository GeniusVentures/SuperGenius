#include "application/bridging_node_application.hpp"
#include <iostream>
#include <csignal>
#include <rapidjson/document.h>
#include "factory/CComponentFactory.hpp"

namespace sgns::application {
    std::atomic<bool> keepRunning(true);

    void signalHandler(int signum) {
        keepRunning = false;
        std::cout << "\nInterrupt signal (" << signum << ") received. Stopping application..." << std::endl;
    }

    void customMessageHandler(const std::string &message) {
        std::cout << "Custom Message Handler: " << message << std::endl;
        // In the future, consensus operations and message processing will be added here
    }

    BridgingNodeApplication::BridgingNodeApplication(const std::shared_ptr<AppConfig> &config,
                                                     const std::shared_ptr<AppStateManager> &stateManager)
            : SgnsApplication(config, stateManager) {
        signal(SIGINT, signalHandler);

        // Initialize components using the factory
        CComponentFactory factory;
        wsContext = std::dynamic_pointer_cast<WsContext>(factory.createComponent("WSContext", config, stateManager));
        wsThreadPool = std::dynamic_pointer_cast<WsThreadPool>(
                factory.createComponent("WSThreadPool", config, stateManager));
        rpcService = std::dynamic_pointer_cast<RpcService>(factory.createComponent("RpcService", config, stateManager));

        // Setup WebSocket and RPC services
        setupWebSocketService();
        setupRpcService();
    }

    void BridgingNodeApplication::run() {
        if (!wsContext || !wsThreadPool || !rpcService) {
            std::cerr << "Failed to initialize required components." << std::endl;
            return;
        }

        // Load the configuration from the AppConfig
        const char *json = R"([
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
        std::vector<EvmMessagingWatcher::TopicFilter> topicFilters = {
                {"BridgeSourceBurned(address,uint256,uint256,uint256,uint256)",
                 "0xa870aa48c12cc82c11a120c39e4d9f99e93e6f74d3e0e0e6eac476e79a6a3663"}
        };

        // The contract address that we want to watch
        std::string contractAddress = "0x1234567890abcdef1234567890abcdef12345678";

        // Create an instance of EvmMessagingWatcher
        evmWatcher = std::make_unique<EvmMessagingWatcher>(config, customMessageHandler, contractAddress, topicFilters);

        // Start watching in a separate thread
        evmWatcher->startWatching();

        std::cout << "BridgingNodeApplication is running. Press Ctrl+C to stop..." << std::endl;

        // Main loop to keep running until interrupted
        while (keepRunning) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        stop();
    }

    void BridgingNodeApplication::stop() {
        if (evmWatcher) {
            evmWatcher->stopWatching();
            std::cout << "Watcher stopped." << std::endl;
        }

        if (rpcService) {
            rpcService->stop();
            std::cout << "RPC Service stopped." << std::endl;
        }
    }

    void BridgingNodeApplication::setupWebSocketService() {
        if (!wsContext || !wsThreadPool) {
            std::cerr << "WebSocket context or thread pool not initialized." << std::endl;
            return;
        }

        // Configure the WebSocket context and thread pool
        wsContext->initialize();
        wsThreadPool->start();
        std::cout << "WebSocket Service initialized." << std::endl;
    }

    void BridgingNodeApplication::setupRpcService() {
        if (!rpcService) {
            std::cerr << "RPC Service not initialized." << std::endl;
            return;
        }

        // Initialize the RPC service
        rpcService->initialize();
        rpcService->start();
        std::cout << "RPC Service initialized." << std::endl;
    }

    void BridgingNodeApplication::handleMessageArrival(const std::string &message) {
        // Placeholder for future implementation of consensus and message processing
        std::cout << "Processing arrived message: " << message << std::endl;
    }
}
