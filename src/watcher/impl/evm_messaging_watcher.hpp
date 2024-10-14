#ifndef EVM_MESSAGING_WATCHER_HPP
#define EVM_MESSAGING_WATCHER_HPP

#include <rapidjson/document.h>
#include <memory>
#include <string>
#include <vector>
#include "watcher/messaging_watcher.hpp"
#include "api/transport/impl/ws/ws_client_impl.hpp"

namespace sgns::evmwatcher {

class EvmMessagingWatcher : public watcher::MessagingWatcher {
    public:
        using TopicFilter = struct {
            std::string topic_hash;
        };

        EvmMessagingWatcher(const rapidjson::Document &config, MessageCallback callback,
                            const std::string &contract_address, const std::vector<TopicFilter> &topic_filters);

        std::string getContractAddress() const;
        void setContractAddress(const std::string &contract_address);

        std::vector<TopicFilter> getTopicFilters() const;
        void setTopicFilters(const std::vector<TopicFilter> &topic_filters);

        void setupWebSocketListener(const ChainConfig &chainConfig);

    private:
        std::string contract_address;
        std::vector<TopicFilter> topic_filters;
        std::shared_ptr<sgns::api::WsClientImpl> ws_client;
        sgns::api::WsClientImpl::Context ws_context;
    };

}  // namespace sgns::evmwatcher

#endif  // EVM_MESSAGING_WATCHER_HPP
