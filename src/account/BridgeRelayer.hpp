/**
 * @file       BridgeRelayer.hpp
 * @brief      Processes bridge burn claims and submits MintFunds transactions.
 * @date       2026-05-30
 */
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "account/TransactionManager.hpp"
#include "base/logger.hpp"
#include "eth/bridge_event.hpp"

namespace sgns
{
    /**
     * @brief Processes BridgeEventClaim objects and calls MintFunds.
     *
     * The BridgeRelayer is a callback receiver — it does not own watchers.
     * Callers wire it to BridgeRpcWatcher::BridgeClaimCallback or similar.
     */
    class BridgeRelayer
    {
    public:
        /**
         * @brief Construct a BridgeRelayer.
         * @param[in] tx_manager TransactionManager to call MintFunds on.
         * @param[in] logger Logger instance.
         */
        BridgeRelayer( std::shared_ptr<TransactionManager> tx_manager, base::Logger logger );

        /**
         * @brief Process a bridge burn claim — calls MintFunds if not duplicate.
         * @param[in] claim The bridge event claim from a watcher.
         */
        void OnBridgeClaim( const eth::BridgeEventClaim &claim );

        /**
         * @brief Returns a callback suitable for BridgeRpcWatcher::BridgeClaimCallback.
         */
        std::function<void( const eth::BridgeEventClaim & )> GetClaimCallback();

    private:
        std::shared_ptr<TransactionManager> tx_manager_;
        base::Logger                        logger_;
    };
} // namespace sgns
