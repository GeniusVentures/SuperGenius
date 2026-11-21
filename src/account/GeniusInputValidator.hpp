/**
 * @file       GeniusInputValidator.hpp
 * @brief      Input validation strategy for native Genius-chain transactions
 * @date       2026-06-02
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_GENIUS_INPUT_VALIDATOR_HPP
#define SGNS_GENIUS_INPUT_VALIDATOR_HPP

#include <memory>

#include "account/InputValidators.hpp"

namespace sgns
{
    class EscrowReleaseTransactionV2;
    class EscrowTransaction;

    /**
     * @brief Validator for native Genius-chain transactions.
     */
    class GeniusInputValidator final : public IInputValidator
    {
    public:
        /// @brief Immutable certified producer, available only with a valid verdict.
        struct EscrowReleaseInput
        {
            WitnessVerdict                           verdict{ WitnessVerdict::kInvalid };
            std::shared_ptr<const EscrowTransaction> producer;
        };

        /**
         * @brief Resolves the sole release input to its actual certified escrow-hold output.
         * @param[in] release Typed payout whose claimed outpoint is checked.
         * @param[in] blockchain Authoritative producer and certificate source.
         * @return Valid with the immutable producer, pending when evidence is missing, otherwise invalid.
         */
        EscrowReleaseInput ResolveEscrowReleaseInput( const EscrowReleaseTransactionV2 &release,
                                                      const Blockchain                 &blockchain ) const;

        /// @copydoc IInputValidator::ValidateTransactionUTXOParameters
        WitnessVerdict ValidateTransactionUTXOParameters( const GeniusTransaction &tx,
                                                          const UTXOTxParameters  &params,
                                                          const std::string       &address,
                                                          const UTXOManager       &utxo_manager,
                                                          const Blockchain        &blockchain ) const override;

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
         * input signatures, ownership, duplicate inputs, registered token identifiers, and raw-value
         * input/output balance.
         *
         * @param[in] subject Consensus subject containing the UTXO witness and commitment.
         * @param[in] tx Genius-chain transaction being validated.
         * @param[in] params UTXO inputs and outputs carried by @p tx.
         * @param[in] blockchain Blockchain service used to resolve producer certificates.
         * @return True when the witness and transaction UTXO parameters are consistent.
         */
        IInputValidator::WitnessVerdict ValidateWitness(
            const ConsensusSubject  &subject,
            const GeniusTransaction &tx,
            const UTXOTxParameters  &params,
            const Blockchain        &blockchain ) const override;

        /**
         * @brief Genius-native validation requires consensus UTXO context.
         * @return Always true.
         */
        bool RequiresConsensusUTXOData() const override
        {
            return true;
        }

        /**
         * @brief Registers the shared instance for the Genius chain IDs.
         * @return      True when the registration ran.
         *
         * Defined out-of-line and triggered before main() by a static
         * initializer in TransactionManager.cpp. It must NOT be triggered from
         * a static initializer in this (exported) header: that constructs a
         * GeniusInputValidator in every including TU, which bakes the vftable
         * (with signature-mangled virtuals) into downstream objects and breaks
         * their link whenever a virtual's signature changes (GeniusWallet
         * LNK2001 on ValidateWitness, 2026-09-25).
         */
        static bool Register();
    };
} // namespace sgns

#endif // SGNS_GENIUS_INPUT_VALIDATOR_HPP
