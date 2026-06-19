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

        bool ValidateWitness( const ConsensusSubject                   &subject,
                              const std::shared_ptr<GeniusTransaction> &tx,
                              const UTXOTxParameters                   &params,
                              const std::shared_ptr<Blockchain>        &blockchain ) const override;

        bool RequiresConsensusUTXOData() const override
        {
            return false;
        }

        /**
         * @brief       Registers this validator in the global registry for the "migration" chain ID.
         * @return      true when the registration is done. This is used to ensure that the static instance is initialized and registered before main() starts.
         */
        static bool Register()
        {
            static MigrationInputValidator instance;
            IInputValidator::Register( "migration", &instance );
            return true;
        }
    };

    /// @brief Static instance to trigger registration of the MigrationInputValidator before main() starts.
    static inline bool kMigrationValidatorRegistered = MigrationInputValidator::Register();
} // namespace sgns

#endif // SGNS_MIGRATION_INPUT_VALIDATOR_HPP
