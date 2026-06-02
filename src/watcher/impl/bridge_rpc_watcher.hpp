// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef BRIDGE_RPC_WATCHER_HPP
#define BRIDGE_RPC_WATCHER_HPP

#include <watcher/messaging_watcher.hpp>

#include <eth/bridge_event.hpp>
#include <eth/rpc_http_transport.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sgns::evmwatcher {

/// @brief RPC-based bridge event watcher that polls eth_getLogs, verifies
///        receipts, and produces normalized BridgeEventClaim objects.
///
/// Unlike EvmMessagingWatcher (which uses WebSocket eth_subscribe), this
/// watcher uses eth::rpc::RpcHttpTransport for JSON-RPC over HTTP.  It
/// polls at a configurable interval, fetches logs for the bridge contract,
/// verifies each event through eth_getTransactionReceipt, and emits
/// eth::BridgeEventClaim objects via a typed callback.
class BridgeRpcWatcher final : public watcher::MessagingWatcher
{
public:
    using BridgeClaimCallback = std::function<void(const eth::BridgeEventClaim &)>;

    struct Config
    {
        std::string rpc_url;
        uint64_t    chain_id = 0;
        uint64_t    dest_chain_id = 0;
        std::string contract_address;
        std::string event_signature;
        uint64_t    confirmation_depth = 12;
        std::chrono::seconds poll_interval{4};
        uint64_t    max_log_range = 1000;
    };

    BridgeRpcWatcher(
        const Config          &config,
        MessageCallback        message_callback,
        BridgeClaimCallback    claim_callback);

    void startWatching() override;
    void stopWatching() override;

    [[nodiscard]] const Config &config() const noexcept { return config_; }
    [[nodiscard]] uint64_t last_processed_block() const noexcept { return last_block_; }

protected:
    void watch() override;

private:
    bool poll_once();

    Config               config_;
    BridgeClaimCallback  claim_callback_;
    eth::rpc::RpcHttpTransport transport_;
    uint64_t             last_block_ = 0;
};

}  // namespace sgns::evmwatcher

#endif  // BRIDGE_RPC_WATCHER_HPP
