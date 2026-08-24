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
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
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
     * @brief Structured, claim-bound evidence of which RPC endpoints actually
     *        verified a specific bridge claim (issue #364).
     *
     * Replaces the previous boolean-only verification result. An endpoint is
     * recorded here only after it passes *every* required check for the exact
     * claim: RPC request completed, receipt parsed, receipt status succeeded,
     * expected bridge contract matched and an accepted topic0 matched.
     *
     * Vote slot hashes are populated exclusively from this evidence, never from
     * endpoint configuration, so a signed vote can no longer claim a DIRECT_API
     * or PUBLIC confirmation from an endpoint that failed, timed out, returned a
     * malformed receipt, or matched the wrong bridge contract.
     */
    struct RpcVerificationEvidence
    {
        /// True only when the weighted successful endpoints reached the quorum.
        bool valid = false;

        /// SHA-256 of the URL of a DIRECT_API endpoint that successfully verified
        /// the claim (empty when no direct endpoint confirmed it).
        std::optional<std::vector<uint8_t>> successful_direct_api;

        /// SHA-256 of the URLs of distinct PUBLIC endpoints that successfully
        /// verified the claim, in the order they confirmed.
        std::vector<std::vector<uint8_t>> successful_public;

        /// Summed consensus_weight of the successful endpoints only.
        int32_t successful_weight = 0;

        /**
         * @brief Returns the hash for a vote slot, drawn solely from successful endpoints.
         *
         * - slot 0: the successful DIRECT_API endpoint, or empty;
         * - slot 1: the first successful PUBLIC endpoint, or empty;
         * - slot 2: a second, distinct successful PUBLIC endpoint, or empty.
         *
         * @param[in] slot_index Vote slot (0, 1 or 2).
         * @return 32-byte SHA-256 of the endpoint URL, or an empty vector (abstain).
         */
        [[nodiscard]] std::vector<uint8_t> SlotHash( size_t slot_index ) const;
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
         * @brief Derives the claim key that binds verification evidence to a subject.
         *
         * The key is the canonical subject id (ConsensusManager::ComputeSubjectId),
         * so evidence produced while validating one proposal's subject can never be
         * read back for a different claim (issue #364, "Claim binding").
         *
         * @param[in] subject Consensus subject carrying the bridge claim.
         * @return Canonical claim key, or std::nullopt when the subject cannot be canonicalised.
         */
        [[nodiscard]] static std::optional<std::string> ClaimKey( const ConsensusSubject &subject );

        /**
         * @brief Consumes the verification evidence recorded for a claim.
         *
         * Evidence is stored by ValidateWitness() once the RPC quorum has run for
         * that exact subject, and is *removed* by this call so it can back exactly
         * one vote and can never be reused for another claim. Thread-safe.
         *
         * @param[in] claim_key Key produced by ClaimKey() for the same subject.
         * @return Recorded evidence, or std::nullopt when no evidence exists for the claim.
         */
        [[nodiscard]] std::optional<RpcVerificationEvidence> TakeEvidence( const std::string &claim_key ) const;

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

        /**
         * @brief Runs the weighted RPC quorum and reports which endpoints confirmed the claim.
         *
         * Every configured endpoint is queried; an endpoint contributes its weight
         * (and its URL hash to the evidence) only after the RPC call completed, the
         * receipt parsed, the receipt status succeeded, the expected bridge contract
         * matched and an accepted topic0 matched. Evidence.valid is true only when
         * the summed successful weight reaches the quorum threshold.
         *
         * @param[in] tx Transaction claiming the public-chain event.
         * @param[in] source_reference Public-chain transaction hash or external source reference.
         * @return Claim-bound verification evidence.
         */
        RpcVerificationEvidence GatherVerificationEvidence( const std::shared_ptr<GeniusTransaction> &tx,
                                                            const std::string &source_reference ) const;

        /// @brief Records evidence for a claim, evicting the oldest entry when full.
        void StoreEvidence( const std::string &claim_key, RpcVerificationEvidence evidence ) const;

        /// @brief Upper bound on cached, not-yet-consumed evidence entries.
        static constexpr size_t kMaxCachedEvidence = 256;

        std::unordered_map<std::string, std::vector<WeightedRpcEndpoint>> rpc_endpoints_;

        /// @brief Claim-bound verification evidence awaiting consumption by CreateVote.
        /// Keyed by ClaimKey(subject); entries are erased on TakeEvidence() and
        /// FIFO-evicted past kMaxCachedEvidence so a stalled claim cannot leak.
        mutable std::unordered_map<std::string, RpcVerificationEvidence> evidence_by_claim_;
        mutable std::deque<std::string>                                  evidence_order_;
        mutable std::mutex                                               evidence_mutex_;

        /// Chain IDs this validator registered for (self-deregistered on destruction).
        std::vector<std::string> registered_chain_ids_;

        /// @brief Pluggable transport factory for DI-based mock injection (D-07, D-14).
        /// When empty, VerifyPublicChainSmartContract uses the default RpcHttpTransport factory.
        mutable TransportFactory transport_factory_;
    };
} // namespace sgns

#endif // SGNS_PUBLIC_CHAIN_INPUT_VALIDATOR_HPP
