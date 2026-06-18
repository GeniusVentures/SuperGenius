/**
 * @file       bridge_rpc_watcher.hpp
 * @brief      Header file for the bridge RPC watcher
 * @date       2026-06-03
 * @author     SuperGenius (ken@gnus.ai)
 * Copyright 2026 Genius Ventures, Inc.
 * SPDX-License-Identifier: MIT
 */

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

namespace sgns::evmwatcher
{

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
        using BridgeClaimCallback = std::function<void( const eth::BridgeEventClaim & )>;

        /**
         * @brief      Configuration structure for BridgeRpcWatcher.
         */
        struct Config
        {
            std::string rpc_url;                 ///< URL of the RPC
            uint64_t    chain_id      = 0;       ///< The source chain ID
            uint64_t    dest_chain_id = 0;       ///< The destination chain ID (Genius)
            std::string contract_address;        ///< The address of the bridge contract
            std::string event_signature;         ///< The signature of the event to listen for
            uint64_t    confirmation_depth = 12; ///< The number of blocks to wait before considering an event confirmed
            std::chrono::seconds poll_interval{ 4 };   ///< The interval at which to poll for new events
            uint64_t             max_log_range = 1000; ///< The maximum range of blocks to query for logs
        };

        /**
         * @brief       Constructs a BridgeRpcWatcher with the specified configuration and callbacks.
         * @param[in]   config Configuration parameters for the watcher.
         * @param[in]   message_callback Callback for raw message handling (inherited from MessagingWatcher).
         * @param[in]   claim_callback Typed callback invoked with parsed BridgeEventClaim objects when events are detected.
         */
        BridgeRpcWatcher( const Config &config, MessageCallback message_callback, BridgeClaimCallback claim_callback );

        void startWatching() override;
        void stopWatching() override;

        /**
         * @brief       Returns the watcher's configuration.
         * @return      Reference to the Config struct used by this watcher.
         */
        [[nodiscard]] const Config &GetConfig() const noexcept
        {
            return config_;
        }

        /**
         * @brief       Returns the last processed block number.
         * @return      Returns the last block number that was processed by the watcher.
         */
        [[nodiscard]] uint64_t GetLastProcessedBlock() const noexcept
        {
            return last_block_;
        }

    protected:
        void watch() override;

    private:
        /**
         * @brief       Performs a single polling cycle: fetches logs from the RPC, verifies receipts, and emits claims.
         */
        void poll_once();

        Config                     config_;         ///< Configuration parameters for the watcher
        BridgeClaimCallback        claim_callback_; ///< Callback for handling parsed bridge event claims
        eth::rpc::RpcHttpTransport transport_;      ///< RPC transport for making HTTP requests
        uint64_t                   last_block_ = 0; ///< Last processed block number
    };

} // namespace sgns::evmwatcher

#endif // BRIDGE_RPC_WATCHER_HPP
