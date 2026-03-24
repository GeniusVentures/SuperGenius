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
        using utxo_merkle::ReadUInt32BE;
        using utxo_merkle::ReadUInt64BE;
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
                                                const base::Hash256                        &pre_root,
                                                const std::shared_ptr<Blockchain>          &blockchain ) const
    {
        if ( !tx || !blockchain )
        {
            return false;
        }
        if ( !subject.has_nonce() || !subject.nonce().has_utxo_witness() )
        {
            return false;
        }

        const auto &inputs  = params.first;
        const auto &outputs = params.second;
        if ( inputs.empty() || outputs.empty() )
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
            if ( payload_owner != tx->GetSrcAddress() )
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
            auto                 current_hash = HashLeaf( payload_vec );
            for ( const auto &step : proof.branch() )
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
                    current_hash = HashNode( sibling_hash_result.value(), current_hash );
                }
                else
                {
                    current_hash = HashNode( current_hash, sibling_hash_result.value() );
                }
            }

            if ( current_hash != pre_root )
            {
                return false;
            }

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
                                                     const base::Hash256                        &pre_root,
                                                     const std::shared_ptr<Blockchain>          &blockchain ) const
    {
        (void)subject;
        (void)tx;
        (void)pre_root;
        (void)blockchain;
        // Placeholder: external proof verification (burn receipt/header finality/replay protection)
        // will live here. For now we only assert explicit input/output presence.
        return !params.first.empty() && !params.second.empty();
    }
} // namespace sgns

