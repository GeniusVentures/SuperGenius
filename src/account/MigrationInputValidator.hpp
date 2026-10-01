/**
 * @file       MigrationInputValidator.hpp
 * @brief      Input validation strategy for one-time migration claims.
 * @date       2026-06-12
 */
#ifndef SGNS_MIGRATION_INPUT_VALIDATOR_HPP
#define SGNS_MIGRATION_INPUT_VALIDATOR_HPP

#include "account/InputValidators.hpp"

namespace sgns
{
    /**
     * @brief      Implements the InputValidator for a Migration type
     */
    class MigrationInputValidator final : public IInputValidator
    {
    public:
        bool ValidateUTXOParameters( const UTXOTxParameters &params,
                                     const std::string      &address,
                                     const UTXOManager      &utxo_manager ) const override;

        IInputValidator::WitnessVerdict ValidateWitness(
            const ConsensusSubject  &subject,
            const GeniusTransaction &tx,
            const UTXOTxParameters  &params,
            const Blockchain        &blockchain ) const override;

        bool RequiresConsensusUTXOData() const override
        {
            return false;
        }

        /**
         * @brief       Registers this validator in the global registry for the "migration" chain ID.
         * @return      true when the registration is done.
         *
         * Defined out-of-line and triggered before main() by a static initializer
         * in TransactionManager.cpp. No static initializer in this header: it
         * would construct a MigrationInputValidator in every including TU and
         * bake the vftable (with signature-mangled virtuals) into those objects,
         * breaking downstream links whenever a virtual's signature changes.
         */
        static bool Register();
    };
} // namespace sgns

#endif // SGNS_MIGRATION_INPUT_VALIDATOR_HPP
