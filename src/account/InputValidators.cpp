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

#include <eth/json_rpc.hpp>
#include <eth/rpc_http_transport.hpp>
#include <base/parse_utility.hpp>
#include <base/rlp-logger.hpp>
#include "base/logger.hpp"

#include <algorithm>

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
        using namespace input_validator_constants;

        std::vector<uint8_t> SerializeOutpointLeafPayload( const base::Hash256 &txid_hash, uint32_t output_index )
        {
            std::vector<uint8_t> payload;
            payload.reserve( HASH256_BYTES + SERIALIZED_UINT32_BYTES );
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
                       PreviewValue( address ), params.first.size(), params.second.size() );

        if ( params.first.empty() || params.second.empty() )
        {
            logger->debug( "ValidateUTXOParameters rejected empty UTXO set: address={} inputs={} outputs={}",
                           PreviewValue( address ), params.first.size(), params.second.size() );
            return false;
        }

        const bool valid = utxo_manager.VerifyParameters( params, address );
        if ( valid )
        {
            logger->info( "ValidateUTXOParameters accepted address={} inputs={} outputs={}",
                          PreviewValue( address ), params.first.size(), params.second.size() );
        }
        else
        {
            logger->debug( "ValidateUTXOParameters failed UTXOManager verification for address={}",
                           PreviewValue( address ) );
        }

        return valid;
    }

    bool GeniusInputValidator::ValidateWitness( const ConsensusSubject                     &subject,
                                                const std::shared_ptr<GeniusTransaction> &tx,
                                                const UTXOTxParameters                     &params,
                                                const std::shared_ptr<Blockchain>          &blockchain ) const
    {
        auto logger = InputValidatorLogger();
        logger->trace( "ValidateWitness(Genius): tx={} inputs={} outputs={}",
                       tx ? PreviewValue( tx->GetHash() ) : "<null>", params.first.size(), params.second.size() );

        if ( !tx || !blockchain )
        {
            logger->error( "ValidateWitness(Genius) missing dependency: tx_present={} blockchain_present={}",
                           tx != nullptr, blockchain != nullptr );
            return false;
        }
        auto nonce_subject = ConsensusManager::DecodeNonceSubject( subject );
        if ( nonce_subject.has_error() ||
             !nonce_subject.value().has_utxo_witness() ||
             !nonce_subject.value().has_utxo_commitment() )
        {
            logger->error( "ValidateWitness(Genius) invalid nonce subject for tx={}",
                           PreviewValue( tx->GetHash() ) );
            return false;
        }

        const auto &inputs  = params.first;
        const auto &outputs = params.second;
        if ( inputs.empty() || outputs.empty() )
        {
            logger->debug( "ValidateWitness(Genius) rejected empty params for tx={}",
                           PreviewValue( tx->GetHash() ) );
            return false;
        }
        const auto tx_hash_result = base::Hash256::fromReadableString( tx->GetHash() );
        if ( tx_hash_result.has_error() )
        {
            logger->error( "ValidateWitness(Genius) invalid tx hash encoding: tx={}",
                           PreviewValue( tx->GetHash() ) );
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
            logger->debug( "ValidateWitness(Genius) commitment size mismatch for tx={}: committed_inputs={} tx_inputs={} committed_outputs={} tx_outputs={}",
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
            if ( !commitment_outpoints.emplace( OutPointKey( out_hash_result.value(), committed_outpoint.output_index() ) ).second )
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
            logger->debug( "ValidateWitness(Genius) consumed root mismatch for tx={}",
                           PreviewValue( tx->GetHash() ) );
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
                logger->debug( "ValidateWitness(Genius) signature verification failed for tx={} input_index={}",
                               PreviewValue( tx->GetHash() ), input.output_idx_ );
                return false;
            }

            auto proof_it = proofs.find( OutPointKey( input.txid_hash_, input.output_idx_ ) );
            if ( proof_it == proofs.end() )
            {
                logger->debug( "ValidateWitness(Genius) missing consumed proof for tx={} input_index={}",
                               PreviewValue( tx->GetHash() ), input.output_idx_ );
                return false;
            }

            const auto outpoint_key = OutPointKey( input.txid_hash_, input.output_idx_ );
            if ( !seen_inputs.insert( outpoint_key ).second )
            {
                logger->debug( "ValidateWitness(Genius) duplicate input detected for tx={} input_index={}",
                               PreviewValue( tx->GetHash() ), input.output_idx_ );
                return false;
            }
            const auto &proof = *proof_it->second;

            const auto &payload = proof.leaf_payload();
            if ( payload.size() <
                 OWNER_ADDRESS_OFFSET + TOKEN_ID_BYTES_IN_PAYLOAD + AMOUNT_BYTES_IN_PAYLOAD )
            {
                logger->debug( "ValidateWitness(Genius) proof payload too short for tx={} input_index={}",
                               PreviewValue( tx->GetHash() ), input.output_idx_ );
                return false;
            }

            auto payload_hash_result = base::Hash256::fromSpan(
                gsl::span( reinterpret_cast<uint8_t *>( const_cast<char *>( payload.data() ) ), HASH256_BYTES ) );
            if ( payload_hash_result.has_error() || payload_hash_result.value() != input.txid_hash_ )
            {
                logger->debug( "ValidateWitness(Genius) proof payload tx hash mismatch for tx={} input_index={}",
                               PreviewValue( tx->GetHash() ), input.output_idx_ );
                return false;
            }
            const auto payload_output_idx =
                ReadUInt32BE( reinterpret_cast<const uint8_t *>( payload.data() ) + OUTPUT_INDEX_OFFSET );
            if ( payload_output_idx != input.output_idx_ )
            {
                logger->debug( "ValidateWitness(Genius) proof payload output index mismatch for tx={} input_index={}",
                               PreviewValue( tx->GetHash() ), input.output_idx_ );
                return false;
            }
            const auto owner_len =
                ReadUInt32BE( reinterpret_cast<const uint8_t *>( payload.data() ) + OWNER_ADDRESS_LENGTH_OFFSET );
            if ( payload.size() <
                 OWNER_ADDRESS_OFFSET + owner_len + TOKEN_ID_BYTES_IN_PAYLOAD + AMOUNT_BYTES_IN_PAYLOAD )
            {
                logger->debug( "ValidateWitness(Genius) proof payload owner length overflow for tx={} input_index={}",
                               PreviewValue( tx->GetHash() ), input.output_idx_ );
                return false;
            }
            const std::string payload_owner( payload.data() + OWNER_ADDRESS_OFFSET,
                                             payload.data() + OWNER_ADDRESS_OFFSET + owner_len );
            const bool delegated_escrow_spend =
                payload_owner != tx->GetSrcAddress() && tx->GetType() == TRANSFER_TX_TYPE &&
                input.output_idx_ == ESCROW_LOCK_OUTPUT_INDEX &&
                utxo_address::IsEscrowLockAddress( payload_owner ) && tx->GetUncleHash() == payload_owner;
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
            const uint64_t    input_amount = ReadUInt64BE( reinterpret_cast<const uint8_t *>( payload.data() ) +
                                                        amount_offset );
            if ( !add_amount( input_amounts_by_token, token_key, input_amount ) )
            {
                logger->error( "ValidateWitness(Genius) input amount overflow for tx={}",
                               PreviewValue( tx->GetHash() ) );
                return false;
            }

            std::vector<uint8_t> payload_vec( payload.begin(), payload.end() );

            auto producer_cert_result = blockchain->GetCertificateBySubjectHash( input.txid_hash_.toReadableString() );
            if ( producer_cert_result.has_error() )
            {
                logger->error( "ValidateWitness(Genius) missing producer certificate for input tx={}",
                               PreviewValue( input.txid_hash_.toReadableString() ) );
                return false;
            }
            const auto &producer_subject = producer_cert_result.value().proposal().subject();
            auto producer_nonce = ConsensusManager::DecodeNonceSubject( producer_subject );
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
            const auto       &token_bytes = output.token_id.bytes();
            const std::string token_key( reinterpret_cast<const char *>( token_bytes.data() ), token_bytes.size() );
            if ( !add_amount( output_amounts_by_token, token_key, output.encrypted_amount ) )
            {
                logger->error( "ValidateWitness(Genius) output amount overflow for tx={}",
                               PreviewValue( tx->GetHash() ) );
                return false;
            }
        }

        if ( input_amounts_by_token != output_amounts_by_token )
        {
            logger->debug( "ValidateWitness(Genius) token balance mismatch for tx={}",
                           PreviewValue( tx->GetHash() ) );
            return false;
        }

        logger->info( "ValidateWitness(Genius) succeeded for tx={}", PreviewValue( tx->GetHash() ) );
        return true;
    }

    bool PublicChainInputValidator::ValidateUTXOParameters( const UTXOTxParameters &params,
                                                            const std::string      &address,
                                                            const UTXOManager      &utxo_manager ) const
    {
        auto logger = InputValidatorLogger();
        (void)address;
        (void)utxo_manager;
        logger->trace( "ValidateUTXOParameters(PublicChain): inputs={} outputs={}",
                       params.first.size(), params.second.size() );
        // Public-chain claims are not validated against local UTXO ownership.
        // We still require input references and minted outputs to be explicit.
        const bool valid = !params.first.empty() && !params.second.empty();
        if ( valid )
        {
            logger->info( "ValidateUTXOParameters(PublicChain) accepted inputs={} outputs={}",
                          params.first.size(), params.second.size() );
        }
        else
        {
            logger->debug( "ValidateUTXOParameters(PublicChain) rejected empty params" );
        }
        return valid;
    }

    bool PublicChainInputValidator::ValidateWitness( const ConsensusSubject                     &subject,
                                                     const std::shared_ptr<GeniusTransaction> &tx,
                                                     const UTXOTxParameters                     &params,
                                                     const std::shared_ptr<Blockchain>          &blockchain ) const
    {
        auto logger = InputValidatorLogger();
        (void)blockchain;
        logger->trace( "ValidateWitness(PublicChain): tx={} inputs={} outputs={}",
                       tx ? PreviewValue( tx->GetHash() ) : "<null>", params.first.size(), params.second.size() );
        if ( !tx || params.first.empty() || params.second.empty() )
        {
            logger->error( "ValidateWitness(PublicChain) invalid inputs: tx_present={} inputs={} outputs={}",
                           tx != nullptr, params.first.size(), params.second.size() );
            return false;
        }

        auto nonce_subject = ConsensusManager::DecodeNonceSubject( subject );
        if ( nonce_subject.has_error() )
        {
            logger->error( "ValidateWitness(PublicChain) failed to decode nonce subject for tx={}",
                           PreviewValue( tx->GetHash() ) );
            return false;
        }

        if ( !nonce_subject.value().has_utxo_commitment() )
        {
            logger->error( "ValidateWitness(PublicChain) missing UTXO commitment for tx={}",
                           PreviewValue( tx->GetHash() ) );
            return false;
        }

        const auto &commitment = nonce_subject.value().utxo_commitment();
        if ( commitment.consumed_outpoints_size() != static_cast<int>( params.first.size() ) ||
             commitment.produced_outputs_size() != static_cast<int>( params.second.size() ) )
        {
            logger->debug( "ValidateWitness(PublicChain) commitment size mismatch for tx={}",
                           PreviewValue( tx->GetHash() ) );
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

        const bool verified = VerifyPublicChainSmartContract( tx, source_reference );
        if ( verified )
        {
            logger->info( "ValidateWitness(PublicChain) succeeded for tx={} source={}",
                          PreviewValue( tx->GetHash() ), PreviewValue( source_reference ) );
        }
        else
        {
            logger->error( "ValidateWitness(PublicChain) failed for tx={} source={}",
                           PreviewValue( tx->GetHash() ), PreviewValue( source_reference ) );
        }
        return verified;
    }

    void PublicChainInputValidator::SetRpcEndpoints( const std::string &chain_id,
                                                     std::vector<WeightedRpcEndpoint> endpoints )
    {
        auto logger = InputValidatorLogger();
        rpc_endpoints_[chain_id] = std::move( endpoints );
        logger->info( "SetRpcEndpoints: chain_id={} endpoint_count={}",
                      chain_id, rpc_endpoints_[chain_id].size() );
    }

    bool PublicChainInputValidator::VerifyPublicChainSmartContract( const std::shared_ptr<GeniusTransaction> &tx,
                                                                    const std::string                        &source_reference ) const
    {
        auto logger = InputValidatorLogger();
        logger->trace( "VerifyPublicChainSmartContract: tx={} chain_id={} source={}",
                       tx ? PreviewValue( tx->GetHash() ) : "<null>",
                       tx ? tx->GetChainId() : "<null>",
                       PreviewValue( source_reference ) );

        if ( source_reference.empty() )
        {
            logger->debug( "VerifyPublicChainSmartContract skipped because source reference is empty" );
            return true;
        }

        const auto chain_id = tx->GetChainId();
        if ( chain_id.empty() || chain_id == "supergenius" )
        {
            logger->debug( "VerifyPublicChainSmartContract bypassed for local chain_id={}", chain_id );
            return true;
        }

        auto chain_it = rpc_endpoints_.find( chain_id );
        if ( chain_it == rpc_endpoints_.end() || chain_it->second.empty() )
        {
            logger->error( "VerifyPublicChainSmartContract has no RPC endpoints for chain_id={}", chain_id );
            return false;
        }

        const auto &endpoints = chain_it->second;

        static constexpr int32_t kRequiredConsensusWeight = 75;
        static constexpr auto    kTimeout                 = std::chrono::seconds( 10 );

        int32_t total_weight   = 0;
        int32_t success_weight = 0;
        size_t  tried          = 0;

        for ( const auto &ep : endpoints )
        {
            total_weight += ep.consensus_weight;

            eth::rpc::RpcHttpTransportOptions opts;
            opts.timeout = kTimeout;
            eth::rpc::RpcHttpTransport transport( ep.url, opts );

            eth::Hash256 tx_hash_parsed{};
            if ( !rlp::base::parse::hex_array( source_reference, tx_hash_parsed ) )
            {
                logger->error( "VerifyPublicChainSmartContract failed to parse source reference {}",
                               PreviewValue( source_reference ) );
                ++tried;
                continue;
            }

            const auto request  = eth::rpc::make_get_transaction_receipt_request( tx_hash_parsed, 1 );
            const auto response = transport.call( request );
            if ( !response.has_value() )
            {
                logger->debug( "VerifyPublicChainSmartContract RPC transport failed for url={}", ep.url );
                ++tried;
                continue;
            }

            const auto receipt = eth::rpc::parse_transaction_receipt_response( response.value() );
            if ( !receipt.has_value() )
            {
                logger->debug( "VerifyPublicChainSmartContract failed to parse receipt from url={}", ep.url );
                ++tried;
                continue;
            }

            if ( !receipt->receipt.status.has_value() || !receipt->receipt.status.value() )
            {
                logger->error( "VerifyPublicChainSmartContract receipt status failed for tx={} via url={}",
                               PreviewValue( source_reference ), ep.url );
                return false;
            }

            // Defense-in-depth: verify receipt logs match expected bridge contract and event topic0.
            // If bridge_contract_address is configured, at least one log must match.
            if ( !ep.bridge_contract_address.empty() )
            {
                bool log_matched = false;
                for ( const auto &log_entry : receipt->receipt.logs )
                {
                    std::string log_addr_hex = rlp::base::parse::hex_array_string( log_entry.address );
                    if ( log_addr_hex == ep.bridge_contract_address &&
                         !log_entry.topics.empty() &&
                         rlp::base::parse::hex_array_string( log_entry.topics.front() ) == ep.event_topic0 )
                    {
                        log_matched = true;
                        break;
                    }
                }
                if ( !log_matched )
                {
                    logger->error( "VerifyPublicChainSmartContract log mismatch bridge={} topic0={} tx={} url={}",
                                   ep.bridge_contract_address,
                                   ep.event_topic0,
                                   PreviewValue( source_reference ),
                                   ep.url );
                    return false;
                }
            }

            success_weight += ep.consensus_weight;
            ++tried;

            if ( success_weight >= kRequiredConsensusWeight )
            {
                logger->info( "VerifyPublicChainSmartContract succeeded: {}/{} weight via {} endpoints for tx={}",
                              success_weight, total_weight, tried, PreviewValue( source_reference ) );
                return true;
            }
        }

        logger->error( "VerifyPublicChainSmartContract insufficient consensus for tx={}: {}/{} weight (need >= {})",
                       PreviewValue( source_reference ), success_weight, total_weight, kRequiredConsensusWeight );
        return false;
    }
} // namespace sgns
