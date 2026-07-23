/**
 * @file       BridgeRelayer.hpp
 * @brief      Wires evmrelay burn events to MintFunds via shared EthWatchService.
 * @date       2026-05-30
 */
#ifndef SGNS_BRIDGE_RELAYER_HPP
#define SGNS_BRIDGE_RELAYER_HPP

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "account/BridgeEventTypes.hpp"
#include "account/ChainContractPair.hpp"
#include "account/ChainRpcEndpointProvider.hpp"
#include "account/TransactionManager.hpp"
#include "base/logger.hpp"
#include "eth/eth_watch_service.hpp"
#include "outcome/outcome.hpp"

/// @brief Forward declaration for unit test access to private members.
class BridgeRelayerTestAccess;

namespace sgns
{
    /**
     * @brief Registers both BridgeSourceBurned (v1) and BridgeOutInitiated (v2)
     *        watches on a shared EthWatchService across multiple chains and calls
     *        MintFunds when burns are detected. OnWatchEvent dispatches on the
     *        variant type of values[5] to handle both event formats (D-06).
     */
    class BridgeRelayer : public IBridgeInitObserver,
                          public std::enable_shared_from_this<BridgeRelayer>
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
         * @brief Parse decoded ABI values into BurnEventParams for bridging.
         *
         * Works for both OnWatchEvent (decoded via decode_log) and catch-up scan
         * (decoded via decode_log).  values layout:
         *   [0] sender (address, indexed)   [3] srcChainID (uint256)
         *   [1] id (uint256)                [4] destChainID (uint256)
         *   [2] amount (uint256)            [5] sgnsDestination (bytes/bytes32)
         *                                    [6] destinationYOdd (bool, v2 only)
         *
         * @param[in] values  Decoded ABI values in declaration order.
         * @return Parsed parameters on success, or error if values are malformed.
         */
        static outcome::result<BurnEventParams> ParseBurnEventValues(
            const std::vector<eth::abi::AbiValue>& values );

        /**
         * @brief Register both v1 (BridgeSourceBurned) and v2 (BridgeOutInitiated)
         *        watches on all provided chains.
         * @param[in] chains Vector of (chain_name, contract_address) pairs.
         *                   Chains without a valid contract address are skipped with a warning.
         *                   Best-effort: if one event/chain fails, others still register (D-21).
         */
        void Start( std::vector<ChainContractPair> chains );

        /**
         * @brief IBridgeInitObserver callback — self-starts when the provider signals readiness.
         * @param[in] chains  List of (chain_name, contract_address, chain_id) pairs.
         */
        void OnRpcEndpointsReady( std::vector<ChainContractPair> chains ) override;

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
        void OnWatchEvent( const eth::WatchEventNotification &notification, const std::string &chain_name );

        std::weak_ptr<TransactionManager> tx_manager_; ///< Weak reference to TransactionManager for calling MintFunds
        std::shared_ptr<eth::EthWatchService> watch_service_; ///< Shared EthWatchService for event detection
        base::Logger                          logger_;        ///< Logger instance for logging within BridgeRelayer
        std::function<outcome::result<std::string>( uint64_t,
                                                    const std::string &,
                                                    const std::string &,
                                                    uint32_t,
                                                    TokenID,
                                                    const std::string & )>
            mint_funds_override_; ///< Unit-test seam for recording mandatory mint identity arguments.
        /// @brief Per-chain watch IDs, keyed by chain name. Populated by Start().
        ///        .first is the v1 (BridgeSourceBurned) watch_id; .second is the
        ///        v2 (BridgeOutInitiated) watch_id. Both registered unconditionally
        ///        per chain (D-15); the wrong-version watch simply never fires.
        std::unordered_map<std::string, std::pair<eth::EventWatchId, eth::EventWatchId>> chain_watches_;
    };
} // namespace sgns

#endif // SGNS_BRIDGE_RELAYER_HPP
