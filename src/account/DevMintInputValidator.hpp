/**
 * @file       DevMintInputValidator.hpp
 * @brief      Production dev-chain input validator: the net-gated twin of
 *             TestMintInputValidator (D-03).
 * @details    Burn-less mints that declare the chain id "dev" route here
 *             through the existing IInputValidator registry. The witness
 *             verdict is kValid iff DevMintBurnlessPermitted() — the
 *             validator's OWN net decides, never the witness content or any
 *             proposer claim — so the same registration that permits dev mints
 *             on staging/local-dev nets refuses them on test and main.
 * @date       2026-10-21
 */

#ifndef SGNS_DEV_MINT_INPUT_VALIDATOR_HPP
#define SGNS_DEV_MINT_INPUT_VALIDATOR_HPP

#include "account/InputValidators.hpp"

namespace sgns
{
    class DevMintInputValidator final : public IInputValidator
    {
    public:
        /**
         * @brief Registers the dev validator for the literal chain id "dev".
         * @return Always true; registration is insert-only, so an already
         *         claimed chain id simply leaves the current owner in place.
         */
        static bool Register();

        /**
         * @brief Accepts explicit input references and minted outputs.
         * @param[in] params UTXO inputs and outputs carried by the transaction.
         * @param[in] address Source address; ignored (no local UTXO ownership).
         * @param[in] utxo_manager Local UTXO manager; ignored.
         * @return True when both input and output lists are non-empty.
         */
        bool ValidateUTXOParameters( const UTXOTxParameters &params,
                                     const std::string      &address,
                                     const UTXOManager      &utxo_manager ) const override;

        /**
         * @brief Net-gated witness verdict: the policy, nothing else.
         * @param[in] subject Consensus subject; ignored.
         * @param[in] tx Transaction; ignored.
         * @param[in] params UTXO parameters; ignored.
         * @param[in] blockchain Blockchain service; ignored.
         * @return kValid iff DevMintBurnlessPermitted(), kInvalid otherwise —
         *         regardless of witness content.
         */
        IInputValidator::WitnessVerdict ValidateWitness( const ConsensusSubject  &subject,
                                                         const GeniusTransaction &tx,
                                                         const UTXOTxParameters  &params,
                                                         const Blockchain        &blockchain ) const override;

        /**
         * @brief Dev mints carry no local UTXO witness (the burn is fabricated
         *         for local seeding; consensus itself is real).
         * @return Always false.
         */
        bool RequiresConsensusUTXOData() const override;
    };
} // namespace sgns

#endif // SGNS_DEV_MINT_INPUT_VALIDATOR_HPP
