#ifndef EVM_MESSAGING_DAPP_HPP
#define EVM_MESSAGING_DAPP_HPP

#include <memory>
#include <string>
#include <vector>
#include "watcher/impl/evm_messaging_watcher.hpp"

class EvmMessagingDapp {
public:
    EvmMessagingDapp(const std::vector<rapidjson::Document> &configs, const std::vector<std::string> &contractAddresses,
                                   const std::vector<std::vector<sgns::evmwatcher::EvmMessagingWatcher::TopicFilter>> &topicFilters);

    void start();
    void stop();

private:
    std::shared_ptr<sgns::evmwatcher::EvmMessagingWatcher> evmWatcher;

};

#endif // EVM_MESSAGING_DAPP_HPP
