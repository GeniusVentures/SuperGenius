/**
 * @file       InputValidators.cpp
 * @brief      Input validation strategies for source chains
 * @date       2026-03-23
 */
#include "account/InputValidators.hpp"

#include <limits>
#include <unordered_map>
#include <unordered_set>

#include "account/GeniusAccount.hpp"
#include "account/UTXOMerkle.hpp"

namespace sgns
{
    namespace
    {
        using utxo_merkle::HashLeaf;
        using utxo_merkle::HashNode;
        using utxo_merkle::OutPointKey;
        using utxo_merkle::AppendUInt32BE;
        using utxo_merkle::AppendUInt64BE;
        using utxo_merkle::ReadUInt32BE;
        using utxo_merkle::ReadUInt64BE;

        std::vector<uint8_t> SerializeOutpointLeafPayload( const base::Hash256 &txid_hash, uint32_t output_index )
        {
            std::vector<uint8_t> payload;
            payload.reserve( 32 + 4 );
            payload.insert( payload.end(), txid_hash.begin(), txid_hash.end() );
            AppendUInt32BE( payload, output_index );
            return payload;
        }

        std::vector<uint8_t> SerializeOutputLeafPayload( const base::Hash256 &txid_hash,
                                                         uint32_t             output_index,
                                                         const std::string   &owner_address,
                                                         gsl::span<const uint8_t> token_bytes,
                                                         uint64_t                 amount )
        {
            std::vector<uint8_t> payload;
            payload.reserve( 32 + 4 + 4 + owner_address.size() + token_bytes.size() + 8 );
            payload.insert( payload.end(), txid_hash.begin(), txid_hash.end() );
            AppendUInt32BE( payload, output_index );
            AppendUInt32BE( payload, static_cast<uint32_t>( owner_address.size() ) );
            payload.insert( payload.end(), owner_address.begin(), owner_address.end() );
            payload.insert( payload.end(), token_bytes.begin(), token_bytes.end() );
            AppendUInt64BE( payload, amount );
            return payload;
        }

        base::Hash256 ComputeMerkleRootFromPayloads( std::vector<std::vector<uint8_t>> payloads )
        {
            if ( payloads.empty() )
            {
                return utxo_merkle::EmptyUTXOMerkleRoot();
            }

            std::sort( payloads.begin(), payloads.end() );
            std::vector<base::Hash256> leaf_hashes;
            leaf_hashes.reserve( payloads.size() );
            for ( const auto &payload : payloads )
            {
                leaf_hashes.push_back( HashLeaf( payload ) );
            }
            return utxo_merkle::ComputeMerkleRootFromLeafHashes( std::move( leaf_hashes ) );
        }

    } // namespace

    bool GeniusInputValidator::ValidateUTXOParameters( const UTXOTxParameters &params,
                                                       const std::string      &address,
                                                       const UTXOManager      &utxo_manager ) const
    {
        if ( params.first.empty() || params.second.empty() )
        {
            return false;
        }

        return utxo_manager.VerifyParameters( params, address );
    }

    bool GeniusInputValidator::ValidateWitness( const ConsensusSubject                     &subject,
                                                const std::shared_ptr<IGeniusTransactions> &tx,
                                                const UTXOTxParameters                     &params,
                                                const std::shared_ptr<Blockchain>          &blockchain ) const
    {
        if ( !tx || !blockchain )
        {
            return false;
        }
        if ( !subject.has_nonce() || !subject.nonce().has_utxo_witness() || !subject.nonce().has_utxo_commitment() )
        {
            return false;
        }

        const auto &inputs  = params.first;
        const auto &outputs = params.second;
        if ( inputs.empty() || outputs.empty() )
        {
            return false;
        }
        const auto tx_hash_result = base::Hash256::fromReadableString( tx->GetHash() );
        if ( tx_hash_result.has_error() )
        {
            return false;
        }
        const auto &commitment = subject.nonce().utxo_commitment();
        if ( commitment.consumed_outpoints_root().size() != base::Hash256::size() ||
             commitment.produced_outputs_root().size() != base::Hash256::size() )
        {
            return false;
        }
        auto consumed_root_result = base::Hash256::fromSpan(
            gsl::span( reinterpret_cast<uint8_t *>( const_cast<char *>( commitment.consumed_outpoints_root().data() ) ),
                       commitment.consumed_outpoints_root().size() ) );
        if ( consumed_root_result.has_error() )
        {
            return false;
        }
        auto produced_root_result = base::Hash256::fromSpan(
            gsl::span( reinterpret_cast<uint8_t *>( const_cast<char *>( commitment.produced_outputs_root().data() ) ),
                       commitment.produced_outputs_root().size() ) );
        if ( produced_root_result.has_error() )
        {
            return false;
        }

        if ( commitment.consumed_outpoints_size() != static_cast<int>( inputs.size() ) ||
             commitment.produced_outputs_size() != static_cast<int>( outputs.size() ) )
        {
            return false;
        }

        std::unordered_set<std::string> commitment_outpoints;
        commitment_outpoints.reserve( commitment.consumed_outpoints_size() );
        std::vector<std::vector<uint8_t>> committed_consumed_payloads;
        committed_consumed_payloads.reserve( commitment.consumed_outpoints_size() );
        for ( const auto &committed_outpoint : commitment.consumed_outpoints() )
        {
            auto out_hash_result = base::Hash256::fromSpan(
                gsl::span( reinterpret_cast<uint8_t *>( const_cast<char *>( committed_outpoint.tx_id_hash().data() ) ),
                           committed_outpoint.tx_id_hash().size() ) );
            if ( out_hash_result.has_error() )
            {
                return false;
            }
            if ( !commitment_outpoints.emplace( OutPointKey( out_hash_result.value(), committed_outpoint.output_index() ) ).second )
            {
                return false;
            }
            committed_consumed_payloads.push_back(
                SerializeOutpointLeafPayload( out_hash_result.value(), committed_outpoint.output_index() ) );
        }

        std::vector<std::vector<uint8_t>> tx_consumed_payloads;
        tx_consumed_payloads.reserve( inputs.size() );
        for ( const auto &input : inputs )
        {
            tx_consumed_payloads.push_back( SerializeOutpointLeafPayload( input.txid_hash_, input.output_idx_ ) );
        }

        if ( ComputeMerkleRootFromPayloads( committed_consumed_payloads ) != consumed_root_result.value() ||
             ComputeMerkleRootFromPayloads( tx_consumed_payloads ) != consumed_root_result.value() )
        {
            return false;
        }

        std::unordered_set<std::string> commitment_outputs;
        commitment_outputs.reserve( commitment.produced_outputs_size() );
        std::vector<std::vector<uint8_t>> committed_produced_payloads;
        committed_produced_payloads.reserve( commitment.produced_outputs_size() );
        for ( const auto &committed_output : commitment.produced_outputs() )
        {
            auto out_hash_result = base::Hash256::fromSpan(
                gsl::span( reinterpret_cast<uint8_t *>( const_cast<char *>( committed_output.tx_id_hash().data() ) ),
                           committed_output.tx_id_hash().size() ) );
            if ( out_hash_result.has_error() )
            {
                return false;
            }
            auto payload = SerializeOutputLeafPayload(
                out_hash_result.value(),
                committed_output.output_index(),
                committed_output.owner_address(),
                gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( committed_output.token_id().data() ),
                                          committed_output.token_id().size() ),
                committed_output.amount() );
            const std::string payload_key( reinterpret_cast<const char *>( payload.data() ), payload.size() );
            if ( !commitment_outputs.emplace( payload_key ).second )
            {
                return false;
            }
            committed_produced_payloads.push_back( std::move( payload ) );
        }

        std::unordered_set<std::string> tx_outputs;
        tx_outputs.reserve( outputs.size() );
        std::vector<std::vector<uint8_t>> tx_produced_payloads;
        tx_produced_payloads.reserve( outputs.size() );
        for ( size_t i = 0; i < outputs.size(); ++i )
        {
            const auto &output      = outputs[i];
            const auto &token_bytes = output.token_id.bytes();
            auto payload = SerializeOutputLeafPayload( tx_hash_result.value(),
                                                       static_cast<uint32_t>( i ),
                                                       output.dest_address,
                                                       gsl::span<const uint8_t>( token_bytes.data(), token_bytes.size() ),
                                                       output.encrypted_amount );
            tx_outputs.emplace( reinterpret_cast<const char *>( payload.data() ), payload.size() );
            tx_produced_payloads.push_back( std::move( payload ) );
        }

        if ( tx_outputs != commitment_outputs ||
             ComputeMerkleRootFromPayloads( committed_produced_payloads ) != produced_root_result.value() ||
             ComputeMerkleRootFromPayloads( tx_produced_payloads ) != produced_root_result.value() )
        {
            return false;
        }

        std::unordered_map<std::string, const ConsumedInputProof *> proofs;
        proofs.reserve( subject.nonce().utxo_witness().consumed_inputs_size() );
        for ( const auto &proof : subject.nonce().utxo_witness().consumed_inputs() )
        {
            auto hash_result = base::Hash256::fromSpan(
                gsl::span( reinterpret_cast<uint8_t *>( const_cast<char *>( proof.tx_id_hash().data() ) ),
                           proof.tx_id_hash().size() ) );
            if ( hash_result.has_error() )
            {
                return false;
            }
            if ( !proofs.emplace( OutPointKey( hash_result.value(), proof.output_index() ), &proof ).second )
            {
                return false;
            }
        }

        const auto add_amount = []( std::unordered_map<std::string, uint64_t> &bucket,
                                    const std::string                         &token_key,
                                    uint64_t                                   amount ) -> bool
        {
            auto &total = bucket[token_key];
            if ( amount > std::numeric_limits<uint64_t>::max() - total )
            {
                return false;
            }
            total += amount;
            return true;
        };

        std::unordered_set<std::string>           seen_inputs;
        std::unordered_map<std::string, uint64_t> input_amounts_by_token;
        std::unordered_map<std::string, uint64_t> output_amounts_by_token;
        seen_inputs.reserve( inputs.size() );
        input_amounts_by_token.reserve( inputs.size() );
        output_amounts_by_token.reserve( outputs.size() );

        for ( const auto &input : inputs )
        {
            if ( !GeniusAccount::VerifySignature(
                     tx->GetSrcAddress(),
                     std::string_view( reinterpret_cast<const char *>( input.signature_.data() ),
                                       input.signature_.size() ),
                     input.SerializeForSigning() ) )
            {
                return false;
            }

            auto proof_it = proofs.find( OutPointKey( input.txid_hash_, input.output_idx_ ) );
            if ( proof_it == proofs.end() )
            {
                return false;
            }

            const auto outpoint_key = OutPointKey( input.txid_hash_, input.output_idx_ );
            if ( !seen_inputs.insert( outpoint_key ).second )
            {
                return false;
            }
            const auto &proof = *proof_it->second;

            const auto &payload = proof.leaf_payload();
            if ( payload.size() < 32 + 4 + 4 + 32 + 8 )
            {
                return false;
            }

            auto payload_hash_result = base::Hash256::fromSpan(
                gsl::span( reinterpret_cast<uint8_t *>( const_cast<char *>( payload.data() ) ), 32 ) );
            if ( payload_hash_result.has_error() || payload_hash_result.value() != input.txid_hash_ )
            {
                return false;
            }
            const auto payload_output_idx = ReadUInt32BE( reinterpret_cast<const uint8_t *>( payload.data() ) + 32 );
            if ( payload_output_idx != input.output_idx_ )
            {
                return false;
            }
            const auto owner_len = ReadUInt32BE( reinterpret_cast<const uint8_t *>( payload.data() ) + 36 );
            if ( payload.size() < 40 + owner_len + 32 + 8 )
            {
                return false;
            }
            const std::string payload_owner( payload.data() + 40, payload.data() + 40 + owner_len );
            const bool delegated_escrow_spend =
                payload_owner != tx->GetSrcAddress() && tx->GetType() == "transfer" && input.output_idx_ == 0 &&
                utxo_address::IsEscrowLockAddress( payload_owner ) && tx->GetUncleHash() == payload_owner;
            if ( payload_owner != tx->GetSrcAddress() && !delegated_escrow_spend )
            {
                return false;
            }
            const size_t      token_offset  = 40 + owner_len;
            const size_t      amount_offset = token_offset + 32;
            const std::string token_key( payload.data() + token_offset, payload.data() + amount_offset );
            const uint64_t    input_amount = ReadUInt64BE( reinterpret_cast<const uint8_t *>( payload.data() ) +
                                                        amount_offset );
            if ( !add_amount( input_amounts_by_token, token_key, input_amount ) )
            {
                return false;
            }

            std::vector<uint8_t> payload_vec( payload.begin(), payload.end() );

            auto producer_cert_result = blockchain->GetCertificateBySubjectHash( input.txid_hash_.toReadableString() );
            if ( producer_cert_result.has_error() )
            {
                return false;
            }
            const auto &producer_subject = producer_cert_result.value().proposal().subject();
            if ( !producer_subject.has_nonce() || !producer_subject.nonce().has_utxo_commitment() )
            {
                return false;
            }
            const auto &producer_commitment = producer_subject.nonce().utxo_commitment();
            if ( producer_commitment.produced_outputs_root().size() != base::Hash256::size() )
            {
                return false;
            }
            auto produced_root_result = base::Hash256::fromSpan( gsl::span(
                reinterpret_cast<uint8_t *>( const_cast<char *>( producer_commitment.produced_outputs_root().data() ) ),
                producer_commitment.produced_outputs_root().size() ) );
            if ( produced_root_result.has_error() )
            {
                return false;
            }

            auto produced_hash = HashLeaf( payload_vec );
            for ( const auto &step : proof.produced_branch() )
            {
                auto sibling_hash_result = base::Hash256::fromSpan(
                    gsl::span( reinterpret_cast<uint8_t *>( const_cast<char *>( step.sibling_hash().data() ) ),
                               step.sibling_hash().size() ) );
                if ( sibling_hash_result.has_error() )
                {
                    return false;
                }

                if ( step.is_left_sibling() )
                {
                    produced_hash = HashNode( sibling_hash_result.value(), produced_hash );
                }
                else
                {
                    produced_hash = HashNode( produced_hash, sibling_hash_result.value() );
                }
            }

            if ( produced_hash != produced_root_result.value() )
            {
                return false;
            }
        }

        for ( const auto &output : outputs )
        {
            const auto       &token_bytes = output.token_id.bytes();
            const std::string token_key( reinterpret_cast<const char *>( token_bytes.data() ), token_bytes.size() );
            if ( !add_amount( output_amounts_by_token, token_key, output.encrypted_amount ) )
            {
                return false;
            }
        }

        if ( input_amounts_by_token != output_amounts_by_token )
        {
            return false;
        }

        return true;
    }

    bool PublicChainInputValidator::ValidateUTXOParameters( const UTXOTxParameters &params,
                                                            const std::string      &address,
                                                            const UTXOManager      &utxo_manager ) const
    {
        (void)address;
        (void)utxo_manager;
        // Public-chain claims are not validated against local UTXO ownership.
        // We still require input references and minted outputs to be explicit.
        return !params.first.empty() && !params.second.empty();
    }

    bool PublicChainInputValidator::ValidateWitness( const ConsensusSubject                     &subject,
                                                     const std::shared_ptr<IGeniusTransactions> &tx,
                                                     const UTXOTxParameters                     &params,
                                                     const std::shared_ptr<Blockchain>          &blockchain ) const
    {
        (void)subject;
        (void)blockchain;
        if ( !tx || params.first.empty() || params.second.empty() )
        {
            return false;
        }

        // Feed the public-chain verification with the explicit input hash.
        // If we had to fallback to an empty Hash256 input, use uncle_hash as external source reference.
        std::string source_reference;
        const auto &input_tx_hash = params.first.front().txid_hash_;
        if ( input_tx_hash != base::Hash256{} )
        {
            source_reference = input_tx_hash.toReadableString();
        }
        else
        {
            source_reference = tx->GetUncleHash();
        }

        return VerifyPublicChainSmartContract( tx, source_reference );
    }

    bool PublicChainInputValidator::VerifyPublicChainSmartContract( const std::shared_ptr<IGeniusTransactions> &tx,
                                                                    const std::string &source_reference ) const
    {
        (void)tx;
        (void)source_reference;
        // Placeholder for real burn/finality/contract validation.
        // Empty source_reference is accepted for bootstrap/test mints where no external source hash is provided yet.
        return true;
    }
} // namespace sgns
