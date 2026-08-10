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
#include <functional>
#include <optional>
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
     * @brief Fetches the chainlist dataset (JSON text) used to discover public
     *        RPC URLs at startup.
     *
     * Production default: HTTPS GET of https://chainid.network/chains.json.
     * Tests inject a callable returning canned JSON (no network).
     *
     * @return The chainlist JSON text, or std::nullopt on fetch failure.
     */
    using ChainlistFetcher = std::function<std::optional<std::string>()>;

    /**
     * @brief Encapsulates ChainList RPC endpoint loading and validator wiring.
     *
     * Reads bridge_chains_config.json at the path provided, extracts chain_id
     * and bridge_contract_address for each chain entry, runtime-fetches public
     * RPC URLs from the chainid.network chainlist dataset (filter: bridge_contract_address + topic0 attached per chain), wires them into
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
         * @brief Overrides the chainlist dataset fetcher (for tests; no network).
         *
         * Must be called before Initialize(). When unset, Initialize() performs
         * a real HTTPS GET of https://chainid.network/chains.json.
         *
         * @param[in] fetcher  Callable returning the chainlist JSON text (or nullopt).
         */
        void SetChainlistFetcher( ChainlistFetcher fetcher )
        {
            chainlist_fetcher_ = std::move( fetcher );
        }

        /**
         * @brief Loads RPC endpoints from bridge_chains_config.json + a runtime
         *        chainlist fetch, wires them into the validator, then calls
         *        IInputValidator::Register per chain.
         *
         * Each fetched public RPC URL contributes 25% consensus weight (≥3 reach
         * the 75-weight quorum). A chainlist fetch failure leaves a chain with no
         * endpoints — it still registers for relayer watch, but receipt
         * verification and catch-up scan fail closed.
         *
         * @param[in] bridge_chains_config_path  Path to bridge_chains_config.json.
         * @param[in] validator                  PublicChainInputValidator to configure.
         * @param[in] is_cancelled               Optional cancellation predicate, checked
         *            after the blocking chainlist fetch and BEFORE publishing (validator
         *            registration + observer notification). Used by GeniusNode to abort a
         *            stale init whose account was switched mid-fetch, so it never publishes
         *            raw validator pointers / notifies freed observers.
         * @return True when at least one chain entry was accepted (had chain_id + bridge_contract_address).
         */
        using CancelChecker    = std::function<bool()>;
        using ObserverCallback = std::function<void( std::vector<ChainContractPair> )>;

        bool Initialize( const std::filesystem::path &bridge_chains_config_path,
                         PublicChainInputValidator   &validator,
                         CancelChecker                is_cancelled = {} );

        void AddObserverCallback( ObserverCallback observer );

    private:
        std::vector<IBridgeInitObserver *> observers_;
        std::vector<ObserverCallback>      observer_callbacks_;
        ChainlistFetcher                   chainlist_fetcher_;
    };
} // namespace sgns

#endif
