/**
 * @file       bridge_catchup_watcher.hpp
 * @brief      Header file for the bridge catch-up scan watcher
 * @date       2026-07-12
 * @author     SuperGenius (ken@gnus.ai)
 * Copyright 2026 Genius Ventures, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Polling watcher that scans historical blocks for unprocessed bridge burn
 * events and forwards them to the node for parsing + minting.  Follows the
 * BridgeRpcWatcher / MessagingWatcher pattern.
 *
 * The watcher keeps its dependency surface small: it decodes raw event logs
 * and passes the ABI values to a BurnProcessor callback provided by the
 * node.  The node (which already links the full account / transaction stack)
 * handles ParseBurnEventValues + MintTokens.  This avoids pulling
 * BridgeRelayer → TransactionManager → ipfs_lite into the watcher target.
 */

#ifndef BRIDGE_CATCHUP_WATCHER_HPP
#define BRIDGE_CATCHUP_WATCHER_HPP

#include <watcher/messaging_watcher.hpp>

#include <account/ChainContractPair.hpp>
#include <eth/abi_decoder.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eth::rpc
{
    class JsonRpcTransport;
}

namespace sgns::evmwatcher
{

    /**
     * @brief Polling watcher that scans bridge chains for historical burn events
     *        and forwards them to the node for parsing + minting.
     *
     * Owns its own boost::thread (via MessagingWatcher).  The watch() loop polls
     * eth_getLogs at a configurable interval, tracks the last processed block per
     * chain, and forwards discovered logs through a typed BurnProcessor callback.
     * The callback implementation (provided by GeniusNode) handles ABI parsing
     * and minting — the watcher itself only decodes the raw log.
     */
    class BridgeCatchupWatcher final : public watcher::MessagingWatcher
    {
    public:
        /**
         * @brief Configuration for the catch-up scan watcher.
         */
        struct Config
        {
            std::chrono::seconds poll_interval{ 15 };          ///< Interval between polling cycles.
            uint64_t             start_block          = 0;  ///< Earliest block to scan (0 = genesis). Test-injected (D-20).
            uint64_t             max_blocks_per_query = 10000; ///< Max block range per eth_getLogs call (matches provider limit).
            uint64_t             max_chunks           = 0;  ///< Max backward chunks per poll (0 = unlimited). Tests use 3.

            /// Optional factory for RPC transport.  When set, poll_once() calls this
            /// instead of constructing a default RpcHttpTransport.  Used by tests to
            /// inject mock / fake transports without needing friend-class access.
            using TransportFactory = std::function<std::unique_ptr<eth::rpc::JsonRpcTransport>( const std::string &url )>;
            TransportFactory transport_factory;  ///< null = use default RpcHttpTransport.
        };

        /**
         * @brief Callback that returns the current set of chains to scan.
         */
        using ChainsProvider = std::function<std::vector<ChainContractPair>()>;

        /**
         * @brief Callback that resolves a chain-id string to an RPC URL.
         */
        using RpcUrlResolver = std::function<std::optional<std::string>( const std::string &chain_id_str )>;

        /**
         * @brief Callback invoked for each discovered burn event.
         *
         * The watcher has already decoded the raw log into ABI values.
         * The callback implementation (in GeniusNode) is responsible for:
         * 1. Calling BridgeRelayer::ParseBurnEventValues(values) to extract
         *    amount / token_id / destination
         * 2. Checking UTXO state (consumed / reserved)
         * 3. Calling MintTokens()
         *
         * @param[in] decoded_values  ABI-decoded log parameters.
         * @param[in] tx_hash_hex     Source-chain transaction hash (hex, no 0x).
         * @param[in] chain_id_str    Source chain ID as a decimal string.
         * @param[in] receipt_log_index Absolute zero-based position in the full receipt.
         * @return true if the burn was successfully submitted for minting.
         */
        using BurnProcessor = std::function<bool( const std::vector<eth::abi::AbiValue> &decoded_values,
                                                  const std::string                     &tx_hash_hex,
                                                  const std::string                     &chain_id_str,
                                                  uint32_t                               receipt_log_index )>;

        /**
         * @brief      Constructs a BridgeCatchupWatcher.
         * @param[in]  config           Polling and start-block configuration.
         * @param[in]  message_callback Callback for raw messages (MessagingWatcher base).
         * @param[in]  chains_provider  Returns the current chain list to scan.
         * @param[in]  rpc_resolver     Resolves a chain-id string to an RPC URL.
         * @param[in]  burn_processor   Called for each discovered burn log.
         */
        BridgeCatchupWatcher( const Config   &config,
                              MessageCallback message_callback,
                              ChainsProvider  chains_provider,
                              RpcUrlResolver  rpc_resolver,
                              BurnProcessor   burn_processor );

        void startWatching() override;
        void stopWatching() override;

        /**
         * @brief      Returns the last processed block for a specific chain.
         * @param[in]  chain_id Numeric chain identifier.
         * @return     Last processed block number, or 0 if never scanned.
         */
        [[nodiscard]] uint64_t GetLastProcessedBlock( uint64_t chain_id ) const noexcept;

    protected:
        void watch() override;

    private:
        void poll_once();

        Config            config_;           ///< Polling and start-block configuration.
        ChainsProvider    chains_provider_;  ///< Returns the current chain list.
        RpcUrlResolver    rpc_resolver_;     ///< Resolves chain-id → RPC URL.
        BurnProcessor     burn_processor_;   ///< Called for each discovered burn log.

        /// Per-chain last processed block number (chain_id → block).
        std::unordered_map<uint64_t, uint64_t> last_block_per_chain_;
        mutable std::mutex                     mutex_;
    };

} // namespace sgns::evmwatcher

#endif // BRIDGE_CATCHUP_WATCHER_HPP
