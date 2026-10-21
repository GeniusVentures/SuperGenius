/**
 * @file       DevMintInputValidator.cpp
 * @brief      Implementation of the net-gated dev-chain input validator (D-03).
 * @date       2026-10-21
 */

#include "account/DevMintInputValidator.hpp"

#include "base/DevMintPolicy.hpp"

namespace sgns
{
    bool DevMintInputValidator::Register()
    {
        static DevMintInputValidator instance;
        IInputValidator::Register( "dev", &instance );
        return true;
    }

    bool DevMintInputValidator::ValidateUTXOParameters( const UTXOTxParameters &params,
                                                        const std::string      &address,
                                                        const UTXOManager      &utxo_manager ) const
    {
        (void) address;
        (void) utxo_manager;
        // The TestMintInputValidator shape: explicit inputs and outputs only.
        // Param binding is re-checked against the subject's commitment by
        // HandleNonceConsensusSubject; strictness here would duplicate it.
        return !params.first.empty() && !params.second.empty();
    }

    IInputValidator::WitnessVerdict DevMintInputValidator::ValidateWitness( const ConsensusSubject &subject,
                                                                            const GeniusTransaction &tx,
                                                                            const UTXOTxParameters  &params,
                                                                            const Blockchain &blockchain ) const
    {
        (void) subject;
        (void) tx;
        (void) params;
        (void) blockchain;
        // The validator's OWN net decides — never the witness content or any
        // proposer claim (Pitfall 5).
        return DevMintBurnlessPermitted() ? IInputValidator::WitnessVerdict::kValid
                                          : IInputValidator::WitnessVerdict::kInvalid;
    }

    bool DevMintInputValidator::RequiresConsensusUTXOData() const
    {
        return false;
    }
} // namespace sgns
