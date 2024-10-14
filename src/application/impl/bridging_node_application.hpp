#ifndef BRIDGING_NODE_APPLICATION_HPP
#define BRIDGING_NODE_APPLICATION_HPP

#include "application/sgns_application.hpp"
#include "application/app_config.hpp"
#include "base/logger.hpp"
#include <memory>

namespace sgns::application {
    class BridgingNodeApplication : public SgnsApplication {
    public:
        BridgingNodeApplication(const std::shared_ptr<AppConfig> &config,
                                const std::shared_ptr<AppStateManager> &stateManager);

        void run() override;

        void stop() override;

    private:
        void setupWebSocketService();

        void setupRpcService();

        void handleMessageArrival(const std::string &message);

        std::unique_ptr<EvmMessagingWatcher> evmWatcher;
        std::shared_ptr<WsContext> wsContext;
        std::shared_ptr<WsThreadPool> wsThreadPool;
        std::shared_ptr<RpcService> rpcService;
    };

}
#endif // BRIDGING_NODE_APPLICATION_HPP
