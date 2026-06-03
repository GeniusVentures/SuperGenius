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
    class BridgeRelayer : public std::enable_shared_from_this<BridgeRelayer>
    {
    public:
        /**
         * @brief       Factory method to create a BridgeRelayer instance with weak TransactionManager reference.
         * @param[in]   tx_manager Weak pointer to the TransactionManager to call MintFunds on.
         * @param[in]   watch_service Shared EthWatchService for event detection.
         * @return      If successful, a shared pointer to the created BridgeRelayer; otherwise, a nullptr
         */
        static std::shared_ptr<BridgeRelayer> Create( std::weak_ptr<TransactionManager>     tx_manager,
                                                      std::shared_ptr<eth::EthWatchService> watch_service );

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
        /// @brief Friend accessor for unit testing OnWatchEvent.
        friend class ::BridgeRelayerTestAccess;
        /**
         * @brief Construct a BridgeRelayer.
         * @param[in] tx_manager TransactionManager to call MintFunds on.
         * @param[in] watch_service Shared EthWatchService for event detection.
         */
        explicit BridgeRelayer( std::weak_ptr<TransactionManager>     tx_manager,
                                std::shared_ptr<eth::EthWatchService> watch_service );
        /**
         * @brief Processes a matched burn event and calls MintFunds.
         * @param[in] notification Watch event with decoded ABI values.
         */
        void OnWatchEvent( const eth::WatchEventNotification &notification );

        std::weak_ptr<TransactionManager> tx_manager_; ///< Weak reference to TransactionManager for calling MintFunds
        std::shared_ptr<eth::EthWatchService> watch_service_; ///< Shared EthWatchService for event detection
        base::Logger                          logger_;        ///< Logger instance for logging within BridgeRelayer
        eth::EventWatchId watch_id_{ 0 }; ///< ID of the registered watch, used for unwatching if needed
    };
} // namespace sgns
