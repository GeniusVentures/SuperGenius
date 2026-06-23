/**
 * @file       ChainRpcEndpointProvider.hpp
 * @brief      Loads RPC endpoints from the evmrelay ChainList provider and wires
 *             them into PublicChainInputValidator with weighted consensus support.
 * @date       2026-05-27
 * @author     SuperGenius
 */
#ifndef _CHAIN_RPC_ENDPOINT_PROVIDER_HPP_
#define _CHAIN_RPC_ENDPOINT_PROVIDER_HPP_

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "account/ChainContractPair.hpp"
#include "account/PublicChainInputValidator.hpp"

namespace sgns
{
    /**
     * @brief Observer interface notified when RPC endpoints have been loaded and wired.
     *
     * Subscribers (e.g. BridgeRelayer) implement this interface and call
     * ChainRpcEndpointProvider::AddObserver() before Initialize() is posted
     * to the io_context. The callback fires synchronously inside Initialize()
     * after all endpoints are wired and IInputValidator::Register has been
     * called for each discovered chain.
     */
    class IBridgeInitObserver
    {
    public:
        /**
         * @brief Called when RPC endpoint initialization completes successfully.
         * @param[in] chains  List of (chain_name, contract_address, chain_id) pairs discovered.
         */
        virtual void OnRpcEndpointsReady( std::vector<ChainContractPair> chains ) = 0;

        virtual ~IBridgeInitObserver() = default;
    };

    /**
     * @brief Encapsulates bridge-chain config loading and validator wiring.
     *
     * Reads bridge_chains_config.json at the path provided, extracts chain_id
     * and bridge_contract_address for each chain entry, attaches the operator-
     * provided RPC URLs from each chain's optional "rpc" array, wires them into
     * PublicChainInputValidator with consensus weights, calls
     * IInputValidator::Register per chain, and notifies IBridgeInitObserver
     * subscribers on success.
     */
    class ChainRpcEndpointProvider
    {
    public:
        ChainRpcEndpointProvider() = default;

        /**
         * @brief Registers an observer to receive the chain/contract list on Init success.
         * @param[in] observer  Non-owning reference to an IBridgeInitObserver.
         *
         * Subscription must occur before Initialize() is posted to the io_context
         * so the callback fires inside that same Initialize() invocation.
         */
        void AddObserver( IBridgeInitObserver &observer );

        /**
         * @brief Loads RPC endpoints from bridge_chains_config.json and wires them
         *        into the validator, then calls IInputValidator::Register per chain.
         *
         * Each URL in a chain's optional "rpc" array contributes 50% consensus
         * weight; ≥2 URLs reach the 75-weight verification quorum. A chain with
         * no "rpc" array is still registered (for relayer watch) but its receipt
         * verification and catch-up scan fail closed (no quorum / no URL).
         *
         * @param[in] bridge_chains_config_path  Path to bridge_chains_config.json.
         * @param[in] validator                  PublicChainInputValidator to configure.
         * @return True when at least one chain entry was accepted (had chain_id + bridge_contract_address).
         */
        bool Initialize( const std::filesystem::path    &bridge_chains_config_path,
                         PublicChainInputValidator       &validator );

    private:
        std::vector<IBridgeInitObserver *> observers_;
    };
} // namespace sgns

#endif