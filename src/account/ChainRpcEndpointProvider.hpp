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

#include "account/PublicChainInputValidator.hpp"
#include "base/logger.hpp"

namespace sgns
{
    /**
     * @brief Platform-agnostic configuration passed to ChainRpcEndpointProvider.
     *
     * On desktop, the app layer loads this from a local JSON config file.
     * On mobile, the app layer constructs it from secure storage or settings.
     * No environment variables or git-tracked secrets are consumed here.
     */
    struct ChainRpcProviderConfig
    {
        /**
         * @brief Filesystem path to the chainid.network ChainList JSON file (array format).
         *
         * This is the aggregated ChainList dataset that provides verified public
         * RPC endpoint URLs for every EVM chain.  The file is typically bundled
         * with the application or downloaded at first launch.
         */
        std::filesystem::path chainlist_json_path;

        /**
         * @brief Per-chain direct (API-key) endpoints supplied by the app layer.
         *
         * Keyed by the numeric chain ID as a string (e.g. "1" for Ethereum).
         * Each entry carries a URL and a consensus weight (typically 50%).
         * API keys must be embedded in the URL (e.g. https://mainnet.infura.io/v3/{key}).
         *
         * These values come from secure storage on mobile or a local-only config
         * file on desktop — never from environment variables or tracked files.
         */
        std::unordered_map<std::string, std::vector<WeightedRpcEndpoint>> direct_endpoints;

        /**
         * @brief Bridge contract addresses keyed by numeric chain ID.
         *
         * Sourced from bridge_chains_config.json's optional "bridge_contract_address" field.
         * Chains not present in this map have no bridge deployed (D-02: skip signal).
         */
        std::unordered_map<uint64_t, std::string> bridge_contract_addresses;

        /**
         * @brief Bridge event topic0 hashes keyed by numeric chain ID.
         *
         * Computed from the BridgeSourceBurned event signature via
         * eth::cli::event_registry(). The topic0 is used by the catch-up scan
         * to construct eth_getLogs queries for historical burns.
         */
        std::unordered_map<uint64_t, std::string> bridge_event_topic0;
    };

    /**
     * @brief Encapsulates ChainList RPC endpoint loading and validator wiring.
     *
     * Loads the chainid.network chains.json, filters to configured chains,
     * groups endpoints by chain ID with consensus weights (public=25%, direct=50%),
     * and calls PublicChainInputValidator::SetRpcEndpoints for each chain.
     */
    class ChainRpcEndpointProvider
    {
    public:
        /**
         * @brief Mapping from config-level chain name to its numeric EVM chain ID.
         */
        using ChainIdMap = std::unordered_map<std::string, uint64_t>;

        /**
         * @brief Constructs the provider with the configured chain name -> ID mapping.
         * @param[in] chain_id_map  Maps config keys (e.g. "ethereum-mainnet") to numeric chain IDs (e.g. 1).
         */
        explicit ChainRpcEndpointProvider( ChainIdMap chain_id_map );

        /**
         * @brief Loads RPC endpoints and wires them into the validator.
         *
         * Public endpoints from the ChainList provider in @p config.chainlist_json_path
         * contribute 25% consensus weight.  Direct endpoints from
         * @p config.direct_endpoints contribute 50% consensus weight.
         *
         * @param[in] validator  PublicChainInputValidator to configure.
         * @param[in] config     Filesystem paths and direct-endpoint definitions.
         * @param[in] logger     Logger for diagnostic output.
         * @return True when at least one chain received RPC endpoints.
         */
        bool Initialize( PublicChainInputValidator  &validator,
                         const ChainRpcProviderConfig &config,
                         const base::Logger          &logger ) const;

    private:
        ChainIdMap chain_id_map_;
    };
} // namespace sgns

#endif