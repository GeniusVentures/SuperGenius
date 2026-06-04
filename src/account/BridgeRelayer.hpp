/**
 * @file       BridgeRelayer.hpp
 * @brief      Wires evmrelay burn events to MintFunds via shared EthWatchService.
 * @date       2026-05-30
 */
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "account/TransactionManager.hpp"
#include "base/logger.hpp"
#include "eth/eth_watch_service.hpp"

/// @brief Forward declaration for unit test access to private members.
class BridgeRelayerTestAccess;

namespace sgns
{
    /**
     * @brief Represents a chain name and its GNUS bridge contract address.
     */
    struct ChainContractPair
    {
        std::string chain_name;
        std::string contract_address;
    };

    /**
     * @brief Registers BridgeSourceBurned watches on a shared EthWatchService
     *        across multiple chains and calls MintFunds when burns are detected.
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
         * @brief Register BridgeSourceBurned watches on all provided chains.
         * @param[in] chains Vector of (chain_name, contract_address) pairs.
         *                   Chains without a valid contract address are skipped with a warning.
         *                   Best-effort: if one chain fails, others still register (D-21).
         */
        void Start( std::vector<ChainContractPair> chains );

        /**
         * @brief Stop watching (currently a no-op — EthWatchService lifecycle is external).
         */
        void Stop();

    private:
        /// @brief Friend accessor for unit testing OnWatchEvent and chain_watches_.
        friend class ::BridgeRelayerTestAccess;
        /**
         * @brief Construct a BridgeRelayer.
         * @param[in] tx_manager TransactionManager to call MintFunds on.
         * @param[in] watch_service Shared EthWatchService for event detection.
         * @param[in] logger Optional injected logger (defaults to BridgeRelayerLogger() if null).
         */
        explicit BridgeRelayer( std::weak_ptr<TransactionManager>     tx_manager,
                                 std::shared_ptr<eth::EthWatchService> watch_service,
                                 base::Logger                          logger = nullptr );
        /**
         * @brief Processes a matched burn event and calls MintFunds.
         * @param[in] notification Watch event with decoded ABI values.
         * @param[in] chain_name Name of the chain that produced this event.
         */
        void OnWatchEvent( const eth::WatchEventNotification &notification,
                           const std::string                 &chain_name );

        std::weak_ptr<TransactionManager> tx_manager_; ///< Weak reference to TransactionManager for calling MintFunds
        std::shared_ptr<eth::EthWatchService> watch_service_; ///< Shared EthWatchService for event detection
        base::Logger                          logger_;        ///< Logger instance for logging within BridgeRelayer
        /// @brief Per-chain watch IDs, keyed by chain name. Populated by Start().
        std::unordered_map<std::string, eth::EventWatchId> chain_watches_;
    };
} // namespace sgns
