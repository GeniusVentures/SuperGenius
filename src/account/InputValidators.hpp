/**
 * @file       InputValidators.hpp
 * @brief      Input validation strategies for different source chains
 * @date       2026-03-23
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "account/GeniusTransaction.hpp"
#include "account/UTXOManager.hpp"
#include "blockchain/Blockchain.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "base/blob.hpp"

namespace sgns
{
    namespace input_validator_constants
    {
        constexpr size_t HASH256_BYTES               = base::Hash256::size();
        constexpr size_t SERIALIZED_UINT32_BYTES     = sizeof( uint32_t );
        constexpr size_t SERIALIZED_UINT64_BYTES     = sizeof( uint64_t );
        constexpr size_t OUTPUT_INDEX_OFFSET         = HASH256_BYTES;
        constexpr size_t OWNER_ADDRESS_LENGTH_OFFSET = OUTPUT_INDEX_OFFSET + SERIALIZED_UINT32_BYTES;
        constexpr size_t OWNER_ADDRESS_OFFSET        = OWNER_ADDRESS_LENGTH_OFFSET + SERIALIZED_UINT32_BYTES;
        constexpr size_t TOKEN_ID_BYTES_IN_PAYLOAD   = HASH256_BYTES;
        constexpr size_t AMOUNT_BYTES_IN_PAYLOAD     = SERIALIZED_UINT64_BYTES;

        constexpr uint32_t         ESCROW_LOCK_OUTPUT_INDEX = 0;
        constexpr std::string_view TRANSFER_TX_TYPE         = "transfer";
    } // namespace input_validator_constants

    /**
     * @brief Strategy interface for validating transaction inputs and their witness data.
     */
    class IInputValidator
    {
    public:
        /**
         * @brief Destroys the input validator.
         */
        virtual ~IInputValidator() = default;

        /**
         * @brief Validates ownership and structure of the supplied UTXO parameters.
         * @param[in] params UTXO inputs and outputs carried by the transaction.
         * @param[in] address Source address expected to own or authorize the inputs.
         * @param[in] utxo_manager Local UTXO manager used for ownership and signature checks when required.
         * @return True when the parameters are structurally valid for this source chain.
         */
        virtual bool ValidateUTXOParameters( const UTXOTxParameters &params,
                                             const std::string      &address,
                                             const UTXOManager      &utxo_manager ) const = 0;

        /**
         * @brief Validates the chain-specific witness data associated with a transaction input set.
         * @param[in] subject Consensus subject that carries nonce, witness, and UTXO commitment data.
         * @param[in] tx Transaction whose inputs and outputs are being validated.
         * @param[in] params UTXO inputs and outputs carried by @p tx.
         * @param[in] blockchain Blockchain service used to resolve producer certificates when required.
         * @return True when the witness proves that @p params are valid for @p tx.
         */
        virtual bool ValidateWitness( const ConsensusSubject                     &subject,
                                      const std::shared_ptr<GeniusTransaction> &tx,
                                      const UTXOTxParameters                     &params,
                                      const std::shared_ptr<Blockchain>          &blockchain ) const = 0;

        /**
         * @brief Indicates whether this validator requires consensus-provided UTXO data.
         * @return True when the validator needs UTXO witness and commitment data from consensus.
         */
        virtual bool RequiresConsensusUTXOData() const = 0;
    };

    /**
     * @brief Validator for native Genius-chain transactions.
     */
    class GeniusInputValidator final : public IInputValidator
    {
    public:
        /**
         * @brief Validates UTXO ownership and signatures for Genius-native inputs.
         * @param[in] params UTXO inputs and outputs carried by the transaction.
         * @param[in] address Source address expected to own or authorize the inputs.
         * @param[in] utxo_manager Local UTXO manager used to verify the inputs.
         * @return True when both input and output lists are non-empty and @p utxo_manager accepts the parameters.
         */
        bool ValidateUTXOParameters( const UTXOTxParameters &params,
                                     const std::string      &address,
                                     const UTXOManager      &utxo_manager ) const override;

        /**
         * @brief Validates witness data against Genius-chain consensus state.
         *
         * Checks the transaction hash, UTXO commitment roots, consumed input proofs,
         * input signatures, ownership, duplicate inputs, and per-token input/output balance.
         *
         * @param[in] subject Consensus subject containing the UTXO witness and commitment.
         * @param[in] tx Genius-chain transaction being validated.
         * @param[in] params UTXO inputs and outputs carried by @p tx.
         * @param[in] blockchain Blockchain service used to resolve producer certificates.
         * @return True when the witness and transaction UTXO parameters are consistent.
         */
        bool ValidateWitness( const ConsensusSubject                     &subject,
                              const std::shared_ptr<GeniusTransaction> &tx,
                              const UTXOTxParameters                     &params,
                              const std::shared_ptr<Blockchain>          &blockchain ) const override;

        /**
         * @brief Genius-native validation requires consensus UTXO context.
         * @return Always true.
         */
        bool RequiresConsensusUTXOData() const override
        {
            return true;
        }
    };

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
    };

    /**
     * @brief Validator for transactions that reference external public-chain proofs.
     */
    class PublicChainInputValidator final : public IInputValidator
    {
    public:
        /**
         * @brief Configure weighted RPC endpoints for a source chain.
         * @param[in] chain_id  Source chain identifier (e.g. "1" for Ethereum).
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
         * @param[in] subject Consensus subject; currently unused by public-chain validation.
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
         * @brief Public-chain validation requires consensus UTXO payloads so
         *        validators independently verify burn receipts via RPC.
         * @return Always true.
         */
        bool RequiresConsensusUTXOData() const override
        {
            return true;
        }

    private:
        /**
         * @brief Verifies that the referenced public-chain smart-contract event matches the transaction
         *        using a weighted multi-provider RPC quorum.
         *
         * Each successful RPC confirmation adds the endpoint's consensus_weight to a running total.
         * Verification passes when the sum reaches >= 75.  Direct endpoints carry 50% weight;
         * public endpoints carry 25% weight.
         *
         * @param[in] tx Transaction claiming the public-chain event.
         * @param[in] source_reference Public-chain transaction hash or external source reference.
         * @return True when the weighted consensus threshold is met.
         */
        bool VerifyPublicChainSmartContract( const std::shared_ptr<GeniusTransaction> &tx,
                                             const std::string                           &source_reference ) const;

        std::unordered_map<std::string, std::vector<WeightedRpcEndpoint>> rpc_endpoints_;
    };
} // namespace sgns
