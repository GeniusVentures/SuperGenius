/**
 * @file       GeniusInputValidator.cpp
 * @brief      Input validation strategy for native Genius-chain transactions
 * @date       2026-06-02
 */
#include "account/GeniusInputValidator.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "account/GeniusAccount.hpp"
#include "account/GeniusTransaction.hpp"
#include "account/TransactionManager.hpp"
#include "account/TokenID.hpp"
#include "account/UTXOManager.hpp"
#include "account/UTXOMerkle.hpp"
#include "blockchain/Blockchain.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "base/logger.hpp"

namespace sgns
{
    namespace
    {
        using utxo_merkle::AppendUInt32BE;
        using utxo_merkle::AppendUInt64BE;
        using utxo_merkle::HashLeaf;
        using utxo_merkle::HashNode;
        using utxo_merkle::OutPointKey;
        using utxo_merkle::ReadUInt32BE;
        using utxo_merkle::ReadUInt64BE;
        using namespace input_validator_constants;

        std::vector<uint8_t> SerializeOutpointLeafPayload( const base::Hash256 &txid_hash, uint32_t output_index )
        {
            std::vector<uint8_t> payload;
            payload.reserve( HASH256_BYTES + SERIALIZED_UINT32_BYTES );
            payload.insert( payload.end(), txid_hash.begin(), txid_hash.end() );
            AppendUInt32BE( payload, output_index );
            return payload;
        }

        std::vector<uint8_t> SerializeOutputLeafPayload( const base::Hash256     &txid_hash,
                                                         uint32_t                 output_index,
                                                         const std::string       &owner_address,
                                                         gsl::span<const uint8_t> token_bytes,
                                                         uint64_t                 amount )
        {
            std::vector<uint8_t> payload;
            payload.reserve( HASH256_BYTES + SERIALIZED_UINT32_BYTES + SERIALIZED_UINT32_BYTES + owner_address.size() +
                             token_bytes.size() + SERIALIZED_UINT64_BYTES );
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

        std::string PreviewValue( const std::string &value, size_t max_length = 12 )
        {
            return value.substr( 0, std::min( value.size(), max_length ) );
        }

        bool IsRegisteredTokenID( std::string_view token_bytes )
        {
            (void) token_bytes;
            return true;
        }

        bool IsRegisteredTokenID( const TokenID &token_id )
        {
            const auto &token_bytes = token_id.bytes();
            return IsRegisteredTokenID(
                std::string_view( reinterpret_cast<const char *>( token_bytes.data() ), token_bytes.size() ) );
        }

    } // namespace

    base::Logger InputValidatorLogger()
    {
        static const auto logger = base::createLogger( "InputValidator" );
        return logger;
    }

    bool GeniusInputValidator::ValidateUTXOParameters( const UTXOTxParameters &params,
                                                       const std::string      &address,
                                                       const UTXOManager      &utxo_manager ) const
    {
        auto logger = InputValidatorLogger();
        logger->trace( "ValidateUTXOParameters: address={} inputs={} outputs={}",
                       PreviewValue( address ),
                       params.first.size(),
                       params.second.size() );

        if ( params.first.empty() || params.second.empty() )
        {
            logger->debug( "ValidateUTXOParameters rejected empty UTXO set: address={} inputs={} outputs={}",
                           PreviewValue( address ),
                           params.first.size(),
                           params.second.size() );
            return false;
        }

        const bool valid = utxo_manager.VerifyParameters( params, address );
        if ( valid )
        {
            logger->info( "ValidateUTXOParameters accepted address={} inputs={} outputs={}",
                          PreviewValue( address ),
                          params.first.size(),
                          params.second.size() );
        }
        else
        {
            logger->debug( "ValidateUTXOParameters failed UTXOManager verification for address={}",
                           PreviewValue( address ) );
        }

        return valid;
    }

    bool GeniusInputValidator::ValidateWitness( const ConsensusSubject                   &subject,
                                                const std::shared_ptr<GeniusTransaction> &tx,
                                                const UTXOTxParameters                   &params,
                                                const std::shared_ptr<Blockchain>        &blockchain ) const
    {
        auto logger = InputValidatorLogger();
        logger->trace( "ValidateWitness(Genius): tx={} inputs={} outputs={}",
                       tx ? PreviewValue( tx->GetHash() ) : "<null>",
                       params.first.size(),
                       params.second.size() );

        if ( !tx || !blockchain )
        {
            logger->error( "ValidateWitness(Genius) missing dependency: tx_present={} blockchain_present={}",
                           tx != nullptr,
                           blockchain != nullptr );
            return false;
        }
        auto nonce_subject = ConsensusManager::DecodeNonceSubject( subject );
        if ( nonce_subject.has_error() || !nonce_subject.value().has_utxo_witness() ||
             !nonce_subject.value().has_utxo_commitment() )
        {
            logger->error( "ValidateWitness(Genius) invalid nonce subject for tx={}", PreviewValue( tx->GetHash() ) );
            return false;
        }

        const auto &inputs  = params.first;
        const auto &outputs = params.second;
        if ( inputs.empty() || outputs.empty() )
        {
            logger->debug( "ValidateWitness(Genius) rejected empty params for tx={}", PreviewValue( tx->GetHash() ) );
            return false;
        }
        const auto tx_hash_result = base::Hash256::fromReadableString( tx->GetHash() );
        if ( tx_hash_result.has_error() )
        {
            logger->error( "ValidateWitness(Genius) invalid tx hash encoding: tx={}", PreviewValue( tx->GetHash() ) );
            return false;
        }
        const auto &commitment = nonce_subject.value().utxo_commitment();
        if ( commitment.consumed_outpoints_root().size() != base::Hash256::size() ||
             commitment.produced_outputs_root().size() != base::Hash256::size() )
        {
            logger->error( "ValidateWitness(Genius) invalid commitment root sizes for tx={}",
                           PreviewValue( tx->GetHash() ) );
            return false;
        }
        auto consumed_root_result = base::Hash256::fromSpan(
            gsl::span( reinterpret_cast<uint8_t *>( const_cast<char *>( commitment.consumed_outpoints_root().data() ) ),
                       commitment.consumed_outpoints_root().size() ) );
        if ( consumed_root_result.has_error() )
        {
            logger->error( "ValidateWitness(Genius) failed to decode consumed root for tx={}",
                           PreviewValue( tx->GetHash() ) );
            return false;
        }
        auto produced_root_result = base::Hash256::fromSpan(
            gsl::span( reinterpret_cast<uint8_t *>( const_cast<char *>( commitment.produced_outputs_root().data() ) ),
                       commitment.produced_outputs_root().size() ) );
        if ( produced_root_result.has_error() )
        {
            logger->error( "ValidateWitness(Genius) failed to decode produced root for tx={}",
                           PreviewValue( tx->GetHash() ) );
            return false;
        }

        if ( commitment.consumed_outpoints_size() != static_cast<int>( inputs.size() ) ||
             commitment.produced_outputs_size() != static_cast<int>( outputs.size() ) )
        {
            logger->debug(
                "ValidateWitness(Genius) commitment size mismatch for tx={}: committed_inputs={} tx_inputs={} committed_outputs={} tx_outputs={}",
                PreviewValue( tx->GetHash() ),
                commitment.consumed_outpoints_size(),
                inputs.size(),
                commitment.produced_outputs_size(),
                outputs.size() );
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
                logger->error( "ValidateWitness(Genius) failed to decode committed consumed outpoint hash for tx={}",
                               PreviewValue( tx->GetHash() ) );
                return false;
            }
            if ( !commitment_outpoints
                      .emplace( OutPointKey( out_hash_result.value(), committed_outpoint.output_index() ) )
                      .second )
            {
                logger->debug( "ValidateWitness(Genius) duplicate committed consumed outpoint for tx={}",
                               PreviewValue( tx->GetHash() ) );
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
            logger->debug( "ValidateWitness(Genius) consumed root mismatch for tx={}", PreviewValue( tx->GetHash() ) );
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
                logger->error( "ValidateWitness(Genius) failed to decode committed produced output hash for tx={}",
                               PreviewValue( tx->GetHash() ) );
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
                logger->debug( "ValidateWitness(Genius) duplicate committed produced output for tx={}",
                               PreviewValue( tx->GetHash() ) );
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
            auto        payload     = SerializeOutputLeafPayload(
                tx_hash_result.value(),
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
            logger->debug( "ValidateWitness(Genius) produced output mismatch for tx={}",
                           PreviewValue( tx->GetHash() ) );
            return false;
        }

        std::unordered_map<std::string, const ConsumedInputProof *> proofs;
        proofs.reserve( nonce_subject.value().utxo_witness().consumed_inputs_size() );
        for ( const auto &proof : nonce_subject.value().utxo_witness().consumed_inputs() )
        {
            auto hash_result = base::Hash256::fromSpan(
                gsl::span( reinterpret_cast<uint8_t *>( const_cast<char *>( proof.tx_id_hash().data() ) ),
                           proof.tx_id_hash().size() ) );
            if ( hash_result.has_error() )
            {
                logger->error( "ValidateWitness(Genius) failed to decode consumed proof hash for tx={}",
                               PreviewValue( tx->GetHash() ) );
                return false;
            }
            if ( !proofs.emplace( OutPointKey( hash_result.value(), proof.output_index() ), &proof ).second )
            {
                logger->debug( "ValidateWitness(Genius) duplicate consumed proof entry for tx={}",
                               PreviewValue( tx->GetHash() ) );
                return false;
            }
        }

        const auto add_amount = []( uint64_t &total, uint64_t amount ) -> bool
        {
            if ( amount > std::numeric_limits<uint64_t>::max() - total )
            {
                return false;
            }
            total += amount;
            return true;
        };

        std::unordered_set<std::string> seen_inputs;
        uint64_t                        input_amount_total  = 0;
        uint64_t                        output_amount_total = 0;
        seen_inputs.reserve( inputs.size() );

        for ( const auto &input : inputs )
        {
            if ( !GeniusAccount::VerifySignature(
                     tx->GetSrcAddress(),
                     std::string_view( reinterpret_cast<const char *>( input.signature_.data() ),
                                       input.signature_.size() ),
                     input.SerializeForSigning() ) )
            {
                logger->debug( "ValidateWitness(Genius) signature verification failed for tx={} input_index={}",
                               PreviewValue( tx->GetHash() ),
                               input.output_idx_ );
                return false;
            }

            auto proof_it = proofs.find( OutPointKey( input.txid_hash_, input.output_idx_ ) );
            if ( proof_it == proofs.end() )
            {
                logger->debug( "ValidateWitness(Genius) missing consumed proof for tx={} input_index={}",
                               PreviewValue( tx->GetHash() ),
                               input.output_idx_ );
                return false;
            }

            const auto outpoint_key = OutPointKey( input.txid_hash_, input.output_idx_ );
            if ( !seen_inputs.insert( outpoint_key ).second )
            {
                logger->debug( "ValidateWitness(Genius) duplicate input detected for tx={} input_index={}",
                               PreviewValue( tx->GetHash() ),
                               input.output_idx_ );
                return false;
            }
            const auto &proof = *proof_it->second;

            const auto &payload = proof.leaf_payload();
            if ( payload.size() < OWNER_ADDRESS_OFFSET + TOKEN_ID_BYTES_IN_PAYLOAD + AMOUNT_BYTES_IN_PAYLOAD )
            {
                logger->debug( "ValidateWitness(Genius) proof payload too short for tx={} input_index={}",
                               PreviewValue( tx->GetHash() ),
                               input.output_idx_ );
                return false;
            }

            auto payload_hash_result = base::Hash256::fromSpan(
                gsl::span( reinterpret_cast<uint8_t *>( const_cast<char *>( payload.data() ) ), HASH256_BYTES ) );
            if ( payload_hash_result.has_error() || payload_hash_result.value() != input.txid_hash_ )
            {
                logger->debug( "ValidateWitness(Genius) proof payload tx hash mismatch for tx={} input_index={}",
                               PreviewValue( tx->GetHash() ),
                               input.output_idx_ );
                return false;
            }
            const auto payload_output_idx = ReadUInt32BE( reinterpret_cast<const uint8_t *>( payload.data() ) +
                                                          OUTPUT_INDEX_OFFSET );
            if ( payload_output_idx != input.output_idx_ )
            {
                logger->debug( "ValidateWitness(Genius) proof payload output index mismatch for tx={} input_index={}",
                               PreviewValue( tx->GetHash() ),
                               input.output_idx_ );
                return false;
            }
            const auto owner_len = ReadUInt32BE( reinterpret_cast<const uint8_t *>( payload.data() ) +
                                                 OWNER_ADDRESS_LENGTH_OFFSET );
            if ( payload.size() <
                 OWNER_ADDRESS_OFFSET + owner_len + TOKEN_ID_BYTES_IN_PAYLOAD + AMOUNT_BYTES_IN_PAYLOAD )
            {
                logger->debug( "ValidateWitness(Genius) proof payload owner length overflow for tx={} input_index={}",
                               PreviewValue( tx->GetHash() ),
                               input.output_idx_ );
                return false;
            }
            const std::string payload_owner( payload.data() + OWNER_ADDRESS_OFFSET,
                                             payload.data() + OWNER_ADDRESS_OFFSET + owner_len );
            const bool        delegated_escrow_spend = payload_owner != tx->GetSrcAddress() &&
                                                tx->GetType() == TRANSFER_TX_TYPE &&
                                                input.output_idx_ == ESCROW_LOCK_OUTPUT_INDEX &&
                                                utxo_address::IsEscrowLockAddress( payload_owner ) &&
                                                tx->GetUncleHash() == payload_owner;
            if ( payload_owner != tx->GetSrcAddress() && !delegated_escrow_spend )
            {
                logger->debug( "ValidateWitness(Genius) owner mismatch for tx={} owner={} src={}",
                               PreviewValue( tx->GetHash() ),
                               PreviewValue( payload_owner ),
                               PreviewValue( tx->GetSrcAddress() ) );
                return false;
            }
            const size_t      token_offset  = OWNER_ADDRESS_OFFSET + owner_len;
            const size_t      amount_offset = token_offset + TOKEN_ID_BYTES_IN_PAYLOAD;
            const std::string token_key( payload.data() + token_offset, payload.data() + amount_offset );
            if ( !IsRegisteredTokenID( token_key ) )
            {
                logger->debug( "ValidateWitness(Genius) unregistered input token for tx={} input_index={}",
                               PreviewValue( tx->GetHash() ),
                               input.output_idx_ );
                return false;
            }
            const uint64_t input_amount = ReadUInt64BE( reinterpret_cast<const uint8_t *>( payload.data() ) +
                                                        amount_offset );
            if ( !add_amount( input_amount_total, input_amount ) )
            {
                logger->error( "ValidateWitness(Genius) input amount overflow for tx={}",
                               PreviewValue( tx->GetHash() ) );
                return false;
            }

            std::vector<uint8_t> payload_vec( payload.begin(), payload.end() );

            const auto producer_hash               = input.txid_hash_.toReadableString();
            auto       producer_transaction_result = TransactionManager::FetchTransaction(
                blockchain->GetGlobalDB(),
                TransactionManager::GetTransactionPath( producer_hash ) );
            if ( producer_transaction_result.has_error() || !producer_transaction_result.value() ||
                 producer_transaction_result.value()->GetHash() != producer_hash )
            {
                logger->error( "ValidateWitness(Genius) missing producer transaction for input tx={}",
                               PreviewValue( producer_hash ) );
                return false;
            }

            auto producer_cert_result = blockchain->GetCertificateBySlot(
                producer_transaction_result.value()->GetSlotID() );
            if ( producer_cert_result.has_error() )
            {
                logger->error( "ValidateWitness(Genius) missing producer certificate for input tx={}",
                               PreviewValue( producer_hash ) );
                return false;
            }
            if ( !TransactionManager::CertificateMatchesTransaction( producer_cert_result.value(),
                                                                     *producer_transaction_result.value() ) )
            {
                logger->error( "ValidateWitness(Genius) producer certificate does not certify input tx={}",
                               PreviewValue( producer_hash ) );
                return false;
            }
            const auto &producer_subject = producer_cert_result.value().proposal().subject();
            auto        producer_nonce   = ConsensusManager::DecodeNonceSubject( producer_subject );
            if ( producer_nonce.has_error() || !producer_nonce.value().has_utxo_commitment() )
            {
                logger->error( "ValidateWitness(Genius) invalid producer nonce subject for input tx={}",
                               PreviewValue( input.txid_hash_.toReadableString() ) );
                return false;
            }
            const auto &producer_commitment = producer_nonce.value().utxo_commitment();
            if ( producer_commitment.produced_outputs_root().size() != base::Hash256::size() )
            {
                logger->error( "ValidateWitness(Genius) invalid producer output root size for input tx={}",
                               PreviewValue( input.txid_hash_.toReadableString() ) );
                return false;
            }
            auto produced_root_result = base::Hash256::fromSpan( gsl::span(
                reinterpret_cast<uint8_t *>( const_cast<char *>( producer_commitment.produced_outputs_root().data() ) ),
                producer_commitment.produced_outputs_root().size() ) );
            if ( produced_root_result.has_error() )
            {
                logger->error( "ValidateWitness(Genius) failed to decode producer output root for input tx={}",
                               PreviewValue( input.txid_hash_.toReadableString() ) );
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
                    logger->error( "ValidateWitness(Genius) failed to decode proof branch sibling for input tx={}",
                                   PreviewValue( input.txid_hash_.toReadableString() ) );
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
                logger->debug( "ValidateWitness(Genius) produced branch root mismatch for input tx={}",
                               PreviewValue( input.txid_hash_.toReadableString() ) );
                return false;
            }
        }

        for ( const auto &output : outputs )
        {
            if ( !IsRegisteredTokenID( output.token_id ) )
            {
                logger->debug( "ValidateWitness(Genius) unregistered output token for tx={}",
                               PreviewValue( tx->GetHash() ) );
                return false;
            }
            if ( !add_amount( output_amount_total, output.encrypted_amount ) )
            {
                logger->error( "ValidateWitness(Genius) output amount overflow for tx={}",
                               PreviewValue( tx->GetHash() ) );
                return false;
            }
        }

        if ( input_amount_total != output_amount_total )
        {
            logger->debug( "ValidateWitness(Genius) value balance mismatch for tx={}: inputs={} outputs={}",
                           PreviewValue( tx->GetHash() ),
                           input_amount_total,
                           output_amount_total );
            return false;
        }

        logger->info( "ValidateWitness(Genius) succeeded for tx={}", PreviewValue( tx->GetHash() ) );
        return true;
    }
} // namespace sgns
