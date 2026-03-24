/**
 * @file       InputValidators.hpp
 * @brief      Input validation strategies for different source chains
 * @date       2026-03-23
 */
#pragma once

#include <memory>
#include <string>

#include "account/IGeniusTransactions.hpp"
#include "account/UTXOManager.hpp"
#include "blockchain/Blockchain.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "base/blob.hpp"

namespace sgns
{
    class IInputValidator
    {
    public:
        virtual ~IInputValidator() = default;

        virtual bool ValidateUTXOParameters( const UTXOTxParameters &params,
                                             const std::string      &address,
                                             const UTXOManager      &utxo_manager ) const = 0;

        virtual bool ValidateWitness( const ConsensusSubject                     &subject,
                                      const std::shared_ptr<IGeniusTransactions> &tx,
                                      const UTXOTxParameters                     &params,
                                      const base::Hash256                        &pre_root,
                                      const std::shared_ptr<Blockchain>          &blockchain ) const = 0;
    };

    class GeniusInputValidator final : public IInputValidator
    {
    public:
        bool ValidateUTXOParameters( const UTXOTxParameters &params,
                                     const std::string      &address,
                                     const UTXOManager      &utxo_manager ) const override;

        bool ValidateWitness( const ConsensusSubject                     &subject,
                              const std::shared_ptr<IGeniusTransactions> &tx,
                              const UTXOTxParameters                     &params,
                              const base::Hash256                        &pre_root,
                              const std::shared_ptr<Blockchain>          &blockchain ) const override;
    };

    class PublicChainInputValidator final : public IInputValidator
    {
    public:
        bool ValidateUTXOParameters( const UTXOTxParameters &params,
                                     const std::string      &address,
                                     const UTXOManager      &utxo_manager ) const override;

        bool ValidateWitness( const ConsensusSubject                     &subject,
                              const std::shared_ptr<IGeniusTransactions> &tx,
                              const UTXOTxParameters                     &params,
                              const base::Hash256                        &pre_root,
                              const std::shared_ptr<Blockchain>          &blockchain ) const override;
    };
} // namespace sgns

