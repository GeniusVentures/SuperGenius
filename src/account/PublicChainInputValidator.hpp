/**
 * @file       PublicChainInputValidator.hpp
 * @brief      Input validation strategy for public-chain source proofs
 * @date       2026-06-02
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "account/InputValidators.hpp"

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
        std::string bridge_contract_address;  ///< Expected bridge contract (hex, "0x...")
        std::string event_topic0;             ///< Expected event topic0 (hex, "0x...")
    };

    /**
     * @brief Validator for transactions that reference external public-chain proofs.
     */
    class PublicChainInputValidator final : public IInputValidator
    {
    public:
        /**
         * @brief Configure weighted RPC endpoints for a source chain.
         * @param[in] chain_id Source chain identifier (e.g. "1" for Ethereum).
         * @param[in] endpoints Weighted RPC endpoint URLs for verifying burn receipts.
         */
        void SetRpcEndpoints( const std::string &chain_id, std::vector<WeightedRpcEndpoint> endpoints );

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
        bool ValidateWitness( const ConsensusSubject                     &subject,
                              const std::shared_ptr<GeniusTransaction> &tx,
                              const UTXOTxParameters                     &params,
                              const std::shared_ptr<Blockchain>          &blockchain ) const override;

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

    private:
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
    };
} // namespace sgns
