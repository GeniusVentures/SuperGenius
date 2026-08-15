/**
 * @file       InputValidators.hpp
 * @brief      Input validation strategy interface for transaction inputs
 * @date       2026-03-23
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_INPUT_VALIDATORS_HPP
#define SGNS_INPUT_VALIDATORS_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "account/UTXOStructs.hpp"
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

    class Blockchain;
    class ConsensusSubject;
    class GeniusTransaction;
    class UTXOManager;

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

        using ValidatorPtr = const IInputValidator *;

        /**
         * @brief Register a validator for a chain if the chain is currently unowned.
         * @return True when the validator was inserted; false when the chain already
         *         has a validator or @p validator is null.
         *
         * Registration is deliberately insert-only. The current owner must remove
         * itself with UnregisterIf() before another validator can claim the chain.
         */
        static bool Register( const std::string &chain_id, ValidatorPtr validator )
        {
            if ( validator == nullptr )
            {
                return false;
            }

            auto &state = registryState();
            std::lock_guard<std::mutex> lock( state.mutex );
            return state.validators.emplace( chain_id, validator ).second;
        }

        /// @brief Remove a chain's registration only if it currently points to
        ///        @p expected (compare-and-remove). This prevents a non-owner
        ///        from removing the current registration and lets a validator
        ///        self-clean on destruction.
        static void UnregisterIf( const std::string &chain_id, ValidatorPtr expected )
        {
            auto &state = registryState();
            std::lock_guard<std::mutex> lock( state.mutex );
            auto it = state.validators.find( chain_id );
            if ( it != state.validators.end() && it->second == expected )
            {
                state.validators.erase( it );
            }
        }

        static ValidatorPtr Get( const std::string &chain_id )
        {
            auto &state = registryState();
            std::lock_guard<std::mutex> lock( state.mutex );
            auto it = state.validators.find( chain_id );
            return it != state.validators.end() ? it->second : nullptr;
        }

    private:
        struct RegistryState
        {
            std::mutex                                    mutex;
            std::unordered_map<std::string, ValidatorPtr> validators;
        };

        static RegistryState &registryState()
        {
            static RegistryState state;
            return state;
        }
    };
} // namespace sgns

#endif // SGNS_INPUT_VALIDATORS_HPP
