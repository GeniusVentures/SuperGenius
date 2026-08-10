/**
 * @file       PublicChainInputValidator.hpp
 * @brief      Input validation strategy for public-chain source proofs
 * @date       2026-06-02
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_PUBLIC_CHAIN_INPUT_VALIDATOR_HPP
#define SGNS_PUBLIC_CHAIN_INPUT_VALIDATOR_HPP

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "account/InputValidators.hpp"

namespace eth::rpc
{
    class JsonRpcTransport;
} // namespace eth::rpc

/// @brief Forward declaration for unit test access to private members.
class PublicChainInputValidatorTestAccess;

namespace sgns
{
    /**
     * @brief Weighted RPC endpoint used for multi-provider consensus verification.
     *
     * Direct (api-key) endpoints contribute 50% weight.
     * Public endpoints from ChainList contribute 25% weight.
     * Verification requires >= 75% weighted consensus across queried endpoints.
     */
    struct WeightedRpcEndpoint
    {
        std::string url;
        uint8_t     consensus_weight = 25;
        std::string bridge_contract_address; ///< Expected bridge contract (hex, "0x...")
        /// Accepted bridge event topic0 hashes (hex, "0x..."). Carries BOTH the
        /// v1 BridgeSourceBurned and v2 BridgeOutInitiated topic0 so witness
        /// validation accepts mints created from either event version.
        std::vector<std::string> accepted_topic0_hashes;
    };

    /**
     * @brief Factory callable that produces a JsonRpcTransport from a URL and timeout.
     *
     * Injected via SetTransportFactory(). When not set, the default factory creates
     * real RpcHttpTransport instances (production path per D-16). Tests inject a
     * factory that returns MockRpcTransport instances (D-07, D-14).
     *
     * @param url    RPC endpoint URL.
     * @param timeout Transport operation timeout.
     * @return Unique ownership of a transport implementing JsonRpcTransport.
     */
    using TransportFactory = std::function<std::unique_ptr<eth::rpc::JsonRpcTransport>( const std::string   &url,
                                                                                        std::chrono::seconds timeout )>;

    /**
     * @brief Validator for transactions that reference external public-chain proofs.
     */
    class PublicChainInputValidator final : public IInputValidator
    {
    public:
        /// @brief Attempts to claim @p chain_id in the global registry and records
        ///        successful claims for self-removal on destruction.
        /// @return True when this validator claimed the chain; false when another
        ///         validator is already registered for it.
        bool RegisterForChain( const std::string &chain_id )
        {
            if ( !IInputValidator::Register( chain_id, this ) )
            {
                return false;
            }
            registered_chain_ids_.push_back( chain_id );
            return true;
        }

        ~PublicChainInputValidator() override
        {
            for ( const auto &chain_id : registered_chain_ids_ )
            {
                IInputValidator::UnregisterIf( chain_id, this );
            }
        }

        /**
         * @brief Configure weighted RPC endpoints for a source chain.
         * @param[in] chain_id Source chain identifier (e.g. "1" for Ethereum).
         * @param[in] endpoints Weighted RPC endpoint URLs for verifying burn receipts.
         */
        void SetRpcEndpoints( const std::string &chain_id, std::vector<WeightedRpcEndpoint> endpoints );

        /**
         * @brief Merge weighted RPC endpoints into a source chain's existing list.
         *
         * Unlike SetRpcEndpoints (wholesale replace), this preserves endpoints
         * already configured for the chain (e.g. operator-supplied private/API-key
         * endpoints from GeniusNode::ConfigureRpcEndpoint) and appends the new
         * ones, deduplicating by URL. Used by the chainlist runtime fetch so an
         * async non-empty fetch never drops higher-weight private endpoints.
         *
         * @param[in] chain_id Source chain identifier (e.g. "1" for Ethereum).
         * @param[in] endpoints Weighted RPC endpoints to merge (URL-deduped).
         */
        void AddRpcEndpoints( const std::string &chain_id, std::vector<WeightedRpcEndpoint> endpoints );

        /**
         * @brief Validates local UTXO structure for externally sourced claims.
         * @param[in] params UTXO inputs and outputs carried by the transaction.
         * @param[in] address Source address; ignored for public-chain validation.
         * @param[in] utxo_manager Local UTXO manager; ignored for public-chain validation.
         * @return True when both input and output lists are non-empty.
         */
        bool ValidateUTXOParameters( const UTXOTxParameters &params,
                                     const std::string      &address,
                                     const UTXOManager      &utxo_manager ) const override;

        /**
         * @brief Validates the external witness data supplied by consensus.
         * @param[in] subject Consensus subject carrying UTXO commitment data.
         * @param[in] tx Transaction that references the public-chain source event.
         * @param[in] params UTXO inputs and outputs carrying the source reference and minted outputs.
         * @param[in] blockchain Blockchain service; currently unused by public-chain validation.
         * @return True when @p tx is present, @p params are non-empty, and the source reference verification succeeds.
         */
        bool ValidateWitness( const ConsensusSubject                   &subject,
                              const std::shared_ptr<GeniusTransaction> &tx,
                              const UTXOTxParameters                   &params,
                              const std::shared_ptr<Blockchain>        &blockchain ) const override;

        /**
         * @brief Public-chain validation does not require local UTXO witness data.
         *
         * Bridge mints use the EVM transaction hash as input, not a local UTXO.
         * Receipt verification is handled via RPC in VerifyPublicChainSmartContract.
         *
         * @return Always false.
         */
        bool RequiresConsensusUTXOData() const override
        {
            return false;
        }

        /**
         * @brief Inject a custom transport factory for DI-based mock support.
         *
         * When set, every call to VerifyPublicChainSmartContract() will use this
         * factory to create transport instances instead of the default
         * RpcHttpTransport factory. Not called in production (D-16).
         *
         * @param[in] factory Callable taking (url, timeout) → unique_ptr<JsonRpcTransport>.
         */
        void SetTransportFactory( TransportFactory factory )
        {
            transport_factory_ = std::move( factory );
        }

        /**
         * @brief Returns the first RPC endpoint URL for a given chain ID, if any exist.
         *
         * Used by the startup catch-up scan to obtain an RPC URL for eth_getLogs
         * queries without needing to re-parse the ChainList provider data.
         *
         * @param[in] chain_id Numeric chain ID as a string (e.g. "1" for Ethereum).
         * @return The first endpoint URL if one exists, std::nullopt otherwise.
         */
        [[nodiscard]] std::optional<std::string> GetFirstRpcUrl( const std::string &chain_id ) const
        {
            auto it = rpc_endpoints_.find( chain_id );
            if ( it != rpc_endpoints_.end() && !it->second.empty() )
            {
                return it->second.front().url;
            }
            return std::nullopt;
        }

        /**
         * @brief Returns the SHA-256 hash of an endpoint URL for a vote slot (Phase 6, D-01).
         *
         * Slot semantics (per the slot-based RPC-hash voting model):
         * - slot 0: first DIRECT_API endpoint (consensus_weight >= 50, D-02).
         * - slot 1: first PUBLIC endpoint (consensus_weight < 50).
         * - slot 2: second PUBLIC endpoint (consensus_weight < 50).
         *
         * The hash is over the endpoint's raw @c url string (UTF-8), NOT the
         * resolved URL nor the bridge_contract_address/event_topic0 fields, so the
         * hash is stable across config reloads and deterministic across peers that
         * share config (T-06-03). An empty vector signals abstention (D-05).
         *
         * @param[in] slot_index  Vote slot (0, 1, or 2).
         * @param[in] chain_id    Source chain identifier.
         * @return 32-byte SHA-256 of the qualifying endpoint URL, or an empty vector
         *         when no qualifying endpoint exists or the slot/chain is unknown.
         */
        [[nodiscard]] std::vector<uint8_t> GetSlotHash( size_t           slot_index,
                                                        const std::string &chain_id ) const noexcept;

        /**
         * @brief Returns the first configured chain id, if any (Phase 6, D-01).
         *
         * Used by GeniusNode's slot-hash populator lambda to resolve a chain
         * context for single-chain deployments (multi-chain resolution is a
         * future enhancement). Read-only and additive (D-10: Tier 1 untouched).
         *
         * @return First configured chain id, or std::nullopt when none configured.
         */
        [[nodiscard]] std::optional<std::string> GetFirstConfiguredChainId() const noexcept
        {
            if ( rpc_endpoints_.empty() )
            {
                return std::nullopt;
            }
            // unordered_map iteration is not order-stable across runs, but for
            // single-chain deployments (the Phase 6 target) there is exactly one
            // entry. Multi-chain resolution will read chain_id from the proposal
            // subject instead.
            return rpc_endpoints_.begin()->first;
        }

    private:
        /// @brief Friend accessor for unit testing VerifyPublicChainSmartContract
        ///        and the wired rpc_endpoints_ (mirrors BridgeRelayerTestAccess).
        friend class ::PublicChainInputValidatorTestAccess;

        /**
         * @brief Verifies that the referenced public-chain smart-contract event matches the transaction
         *        using a weighted multi-provider RPC quorum.
         *
         * Each successful RPC confirmation adds the endpoint's consensus_weight to a running total.
         * Verification passes when the sum reaches >= 75. Direct endpoints carry 50% weight;
         * public endpoints carry 25% weight.
         *
         * @param[in] tx Transaction claiming the public-chain event.
         * @param[in] source_reference Public-chain transaction hash or external source reference.
         * @return True when the weighted consensus threshold is met.
         */
        bool VerifyPublicChainSmartContract( const std::shared_ptr<GeniusTransaction> &tx,
                                             const std::string                        &source_reference ) const;

        std::unordered_map<std::string, std::vector<WeightedRpcEndpoint>> rpc_endpoints_;

        /// Chain IDs this validator registered for (self-deregistered on destruction).
        std::vector<std::string> registered_chain_ids_;

        /// @brief Pluggable transport factory for DI-based mock injection (D-07, D-14).
        /// When empty, VerifyPublicChainSmartContract uses the default RpcHttpTransport factory.
        mutable TransportFactory transport_factory_;
    };
} // namespace sgns

#endif // SGNS_PUBLIC_CHAIN_INPUT_VALIDATOR_HPP
