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
         * input signatures, ownership, duplicate inputs, registered token identifiers, and raw-value
         * input/output balance.
         *
         * @param[in] subject Consensus subject containing the UTXO witness and commitment.
         * @param[in] tx Genius-chain transaction being validated.
         * @param[in] params UTXO inputs and outputs carried by @p tx.
         * @param[in] blockchain Blockchain service used to resolve producer certificates.
         * @return True when the witness and transaction UTXO parameters are consistent.
         */
        bool ValidateWitness( const ConsensusSubject                   &subject,
                              const std::shared_ptr<GeniusTransaction> &tx,
                              const UTXOTxParameters                   &params,
                              const std::shared_ptr<Blockchain>        &blockchain ) const override;

        /**
         * @brief Genius-native validation requires consensus UTXO context.
         * @return Always true.
         */
        bool RequiresConsensusUTXOData() const override
        {
            return true;
        }

        static bool Register()
        {
            static GeniusInputValidator instance;
            IInputValidator::Register( "supergenius", &instance );
            IInputValidator::Register( "supergenius_chain", &instance );
            IInputValidator::Register( "", &instance );
            return true;
        }
    };

    static inline bool kGeniusValidatorRegistered = GeniusInputValidator::Register();
} // namespace sgns

#endif // SGNS_GENIUS_INPUT_VALIDATOR_HPP
