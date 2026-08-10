/**
 * @file       MigrationInputValidator.cpp
 * @brief      Input validation strategy for one-time migration claims.
 * @date       2026-06-12
 */
#include "account/MigrationInputValidator.hpp"

#include <cstring>
#include <string>
#include <vector>

#include "account/GeniusTransaction.hpp"
#include "account/MigrationTransaction.hpp"
#include "account/UTXOMerkle.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"

namespace sgns
{
    namespace
    {
        bool HasValidMigrationShape( const UTXOTxParameters &params )
        {
            return params.first.size() == 1 && params.second.size() == 1 && params.first.front().output_idx_ == 0 &&
                   params.first.front().signature_.empty() && params.second.front().encrypted_amount > 0 &&
                   !params.second.front().dest_address.empty();
        }

        std::vector<uint8_t> SerializeOutpoint( const InputUTXOInfo &input )
        {
            std::vector<uint8_t> payload;
            payload.reserve( base::Hash256::size() + sizeof( uint32_t ) );
            payload.insert( payload.end(), input.txid_hash_.begin(), input.txid_hash_.end() );
            utxo_merkle::AppendUInt32BE( payload, input.output_idx_ );
            return payload;
        }

        bool MatchesCommittedOutpoint( const UTXOTransitionCommitment::CommittedOutPoint &committed,
                                       const InputUTXOInfo                               &input )
        {
            return committed.tx_id_hash().size() == base::Hash256::size() &&
                   std::memcmp( committed.tx_id_hash().data(), input.txid_hash_.data(), base::Hash256::size() ) == 0 &&
                   committed.output_index() == input.output_idx_;
        }
    } // namespace

    bool MigrationInputValidator::ValidateUTXOParameters( const UTXOTxParameters &params,
                                                          const std::string      &address,
                                                          const UTXOManager      &utxo_manager ) const
    {
        (void) address;
        (void) utxo_manager;
        return HasValidMigrationShape( params );
    }

    bool MigrationInputValidator::ValidateWitness( const ConsensusSubject                   &subject,
                                                   const std::shared_ptr<GeniusTransaction> &tx,
                                                   const UTXOTxParameters                   &params,
                                                   const std::shared_ptr<Blockchain>        &blockchain ) const
    {
        (void) blockchain;
        auto migration_tx = std::dynamic_pointer_cast<MigrationTransaction>( tx );
        if ( !migration_tx || !HasValidMigrationShape( params ) )
        {
            return false;
        }

        const auto expected_source = MigrationTransaction::DeriveUniqueSourceKey( migration_tx->GetFromVersion(),
                                                                                  tx->GetSrcAddress(),
                                                                                  migration_tx->GetTokenID() );
        if ( params.first.front().txid_hash_.toReadableString() != expected_source ||
             tx->GetUncleHash() != expected_source ||
             params.second.front().encrypted_amount != migration_tx->GetAmount() ||
             params.second.front().token_id != migration_tx->GetTokenID() )
        {
            return false;
        }

        auto nonce_subject = ConsensusManager::DecodeNonceSubject( subject );
        if ( nonce_subject.has_error() || !nonce_subject.value().has_utxo_commitment() )
        {
            return false;
        }

        const auto &commitment = nonce_subject.value().utxo_commitment();
        if ( commitment.consumed_outpoints_size() != 1 || commitment.produced_outputs_size() != 1 ||
             !MatchesCommittedOutpoint( commitment.consumed_outpoints( 0 ), params.first.front() ) )
        {
            return false;
        }

        const auto &committed_output = commitment.produced_outputs( 0 );
        const auto  tx_hash          = base::Hash256::fromReadableString( tx->GetHash() );
        const auto &output           = params.second.front();
        const auto &token_bytes      = output.token_id.bytes();
        if ( tx_hash.has_error() || committed_output.tx_id_hash().size() != base::Hash256::size() ||
             std::memcmp( committed_output.tx_id_hash().data(), tx_hash.value().data(), base::Hash256::size() ) != 0 ||
             committed_output.output_index() != 0 || committed_output.owner_address() != output.dest_address ||
             committed_output.token_id() != std::string( token_bytes.begin(), token_bytes.end() ) ||
             committed_output.amount() != output.encrypted_amount )
        {
            return false;
        }

        const auto consumed_root = utxo_merkle::ComputeMerkleRootFromPayloads(
            { SerializeOutpoint( params.first.front() ) } );
        const GeniusUTXO produced_utxo( tx_hash.value(),
                                        0,
                                        output.encrypted_amount,
                                        output.token_id,
                                        output.dest_address );
        const auto       produced_root = utxo_merkle::ComputeMerkleRootFromPayloads(
            { utxo_merkle::SerializeUTXOLeafPayload( produced_utxo ) } );

        return commitment.consumed_outpoints_root() == std::string( consumed_root.begin(), consumed_root.end() ) &&
               commitment.produced_outputs_root() == std::string( produced_root.begin(), produced_root.end() );
    }
} // namespace sgns
