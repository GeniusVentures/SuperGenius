/**
 * @file       BridgeRelayer.hpp
 * @brief      Wires evmrelay burn events to MintFunds via shared EthWatchService.
 * @date       2026-05-30
 */
#pragma once

#include <memory>
#include <string>

#include "account/TransactionManager.hpp"
#include "base/logger.hpp"
#include "eth/eth_watch_service.hpp"

/// @brief Forward declaration for unit test access to private members.
class BridgeRelayerTestAccess;

namespace sgns
{
    /**
     * @brief Registers a BridgeSourceBurned watch on a shared EthWatchService
     *        and calls MintFunds when burns are detected.
     */
    class BridgeRelayer
    {
    public:
        /**
         * @brief Construct a BridgeRelayer.
         * @param[in] tx_manager TransactionManager to call MintFunds on.
         * @param[in] watch_service Shared EthWatchService for event detection.
         * @param[in] logger Logger instance.
         */
        BridgeRelayer( std::shared_ptr<TransactionManager>  tx_manager,
                       std::shared_ptr<eth::EthWatchService> watch_service,
                       base::Logger                          logger );

        /**
         * @brief Register the BridgeSourceBurned watch on the EthWatchService.
         * @param[in] chain_name Chain name for logging (e.g. "ethereum-mainnet").
         * @param[in] contract_address GNUS bridge contract address.
         */
        void Start( const std::string &chain_name, const std::string &contract_address );

        /**
         * @brief Stop watching (currently a no-op — EthWatchService lifecycle is external).
         */
        void Stop();

    private:
        /**
         * @brief Processes a matched burn event and calls MintFunds.
         * @param[in] notification Watch event with decoded ABI values.
         */
        void OnWatchEvent( const eth::WatchEventNotification &notification );

        /// @brief Friend accessor for unit testing OnWatchEvent.
        friend class ::BridgeRelayerTestAccess;

        std::shared_ptr<TransactionManager>  tx_manager_;
        std::shared_ptr<eth::EthWatchService> watch_service_;
        base::Logger                          logger_;
        eth::EventWatchId                     watch_id_{ 0 };
    };
} // namespace sgns
