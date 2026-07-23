/**
 * @file       PublicChainInputValidator.cpp
 * @brief      Input validation strategy for public-chain source proofs
 * @date       2026-06-02
 */
#include "account/PublicChainInputValidator.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <unordered_set>
#include <utility>

#include <base/parse_utility.hpp>
#include <base/rlp-logger.hpp>
#include <crypto/hasher.hpp>
#include <eth/json_rpc.hpp>
#include <eth/rpc_http_transport.hpp>
#include <eth/eth_watch_cli.hpp>

#include "account/BridgeEventTypes.hpp"
#include "account/BridgeRelayer.hpp"
#include "account/GeniusTransaction.hpp"
#include "account/MintTransactionV2.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "base/blob.hpp"
#include "base/logger.hpp"

namespace sgns
{
    namespace
    {
        std::string PreviewValue( const std::string &value, size_t max_length = 12 )
        {
            return value.substr( 0, std::min( value.size(), max_length ) );
        }

        base::Logger InputValidatorLogger()
        {
            static const auto logger = base::createLogger( "InputValidator" );
            return logger;
        }
    } // namespace

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
        const bool valid = params.first.size() == 1 && params.second.size() == 1;
        if ( valid )
        {
            logger->info( "ValidateUTXOParameters(PublicChain) accepted inputs={} outputs={}",
                          params.first.size(), params.second.size() );
        }
        else
        {
            logger->debug( "ValidateUTXOParameters(PublicChain) requires exactly one input and one output" );
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

    void PublicChainInputValidator::AddRpcEndpoints( const std::string &chain_id,
                                                      std::vector<WeightedRpcEndpoint> endpoints )
    {
        auto        logger = InputValidatorLogger();
        auto       &existing = rpc_endpoints_[chain_id];

        // The fetched endpoints carry the chain's canonical {v1, v2} topic set
        // (and bridge contract). That set is a per-chain property — every
        // endpoint for the chain validates the same bridge events — so union it
        // into EVERY existing endpoint, not just URL duplicates. Otherwise a
        // stale v1-only operator endpoint at a different URL keeps failing v2
        // receipts and, being high-weight, can break quorum even though the
        // fetch supplied the missing v2 metadata.
        std::vector<std::string> fetched_topics;
        std::string              fetched_bridge;
        for ( const auto &e : endpoints )
        {
            for ( const auto &h : e.accepted_topic0_hashes )
            {
                if ( std::find( fetched_topics.begin(), fetched_topics.end(), h ) == fetched_topics.end() )
                {
                    fetched_topics.push_back( h );
                }
            }
            if ( fetched_bridge.empty() && !e.bridge_contract_address.empty() )
            {
                fetched_bridge = e.bridge_contract_address;
            }
        }

        size_t upgraded = 0;
        for ( auto &cur : existing )
        {
            bool changed = false;
            for ( const auto &h : fetched_topics )
            {
                if ( std::find( cur.accepted_topic0_hashes.begin(),
                                cur.accepted_topic0_hashes.end(),
                                h ) == cur.accepted_topic0_hashes.end() )
                {
                    cur.accepted_topic0_hashes.push_back( h );
                    changed = true;
                }
            }
            if ( cur.bridge_contract_address.empty() && !fetched_bridge.empty() )
            {
                cur.bridge_contract_address = fetched_bridge;
                changed = true;
            }
            if ( changed )
            {
                ++upgraded;
            }
        }

        // Append fetched endpoints whose URL isn't already present (dedup by URL).
        std::unordered_set<std::string> seen_urls;
        seen_urls.reserve( existing.size() + endpoints.size() );
        for ( const auto &e : existing )
        {
            seen_urls.insert( e.url );
        }
        size_t added = 0;
        for ( auto &e : endpoints )
        {
            if ( seen_urls.insert( e.url ).second )
            {
                existing.push_back( std::move( e ) );
                ++added;
            }
        }

        logger->info( "AddRpcEndpoints: chain_id={} added={} upgraded={} total={}",
                      chain_id, added, upgraded, existing.size() );
    }

    std::vector<uint8_t>
        PublicChainInputValidator::GetSlotHash( size_t slot_index, const std::string &chain_id ) const noexcept
    {
        auto logger = InputValidatorLogger();

        // Phase 6 (D-01): consensus_weight threshold separating DIRECT_API from PUBLIC slots.
        static constexpr uint8_t kDirectApiWeightThreshold = 50;

        const auto chain_it = rpc_endpoints_.find( chain_id );
        if ( chain_it == rpc_endpoints_.end() )
        {
            logger->debug( "GetSlotHash: no endpoints for chain_id={} slot={}", chain_id, slot_index );
            return {};
        }

        const auto &endpoints = chain_it->second;
        std::vector<uint8_t>  result;

        // Slot selection does not modify endpoint state and SHA-256 over an
        // in-memory string does not throw, so the whole accessor is noexcept.
        const auto hash_url = []( const std::string &url ) noexcept -> std::vector<uint8_t> {
            const auto digest = crypto::sha2_256(
                reinterpret_cast<const uint8_t *>( url.data() ), url.size() );
            return std::vector<uint8_t>( digest.begin(), digest.end() );
        };

        switch ( slot_index )
        {
            case 0:
            {
                // First DIRECT_API endpoint (consensus_weight >= 50).
                for ( const auto &ep : endpoints )
                {
                    if ( ep.consensus_weight >= kDirectApiWeightThreshold )
                    {
                        result = hash_url( ep.url );
                        break;
                    }
                }
                break;
            }
            case 1:
            {
                // First PUBLIC endpoint (consensus_weight < 50).
                for ( const auto &ep : endpoints )
                {
                    if ( ep.consensus_weight < kDirectApiWeightThreshold )
                    {
                        result = hash_url( ep.url );
                        break;
                    }
                }
                break;
            }
            case 2:
            {
                // Second PUBLIC endpoint (consensus_weight < 50).
                size_t public_seen = 0;
                for ( const auto &ep : endpoints )
                {
                    if ( ep.consensus_weight < kDirectApiWeightThreshold )
                    {
                        ++public_seen;
                        if ( public_seen == 2 )
                        {
                            result = hash_url( ep.url );
                            break;
                        }
                    }
                }
                break;
            }
            default:
            {
                // Unknown slot index: fail-closed -> abstention (empty vector).
                logger->debug( "GetSlotHash: unknown slot_index={} chain_id={}", slot_index, chain_id );
                return {};
            }
        }

        if ( result.empty() )
        {
            logger->debug( "GetSlotHash: no qualifying endpoint for slot={} chain_id={} (abstain)", slot_index, chain_id );
        }
        return result;
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
        const auto mint_tx = std::dynamic_pointer_cast<MintTransactionV2>( tx );
        if ( !mint_tx )
        {
            logger->error( "VerifyPublicChainSmartContract requires a mint-v2 transaction" );
            return false;
        }
        const auto mint_params = mint_tx->GetUTXOParameters();
        if ( mint_params.first.size() != 1 || mint_params.second.size() != 1 )
        {
            logger->error( "VerifyPublicChainSmartContract requires exactly one bridge input and output" );
            return false;
        }

        uint64_t expected_chain_id = 0;
        try
        {
            size_t parsed_chars = 0;
            expected_chain_id = std::stoull( chain_id, &parsed_chars, 10 );
            if ( parsed_chars != chain_id.size() )
            {
                return false;
            }
        }
        catch ( const std::exception & )
        {
            logger->error( "VerifyPublicChainSmartContract invalid chain id {}", chain_id );
            return false;
        }

        const auto &mint_input = mint_params.first.front();
        const auto &mint_output = mint_params.second.front();

        static constexpr int32_t kRequiredConsensusWeight = 75;
        static constexpr auto    kTimeout                 = std::chrono::seconds( 10 );

        // Resolve transport factory: use injected factory if set (D-07, D-14),
        // otherwise default to real RpcHttpTransport (production path per D-16).
        auto factory = transport_factory_
                           ? transport_factory_
                           : []( const std::string &url, std::chrono::seconds timeout ) {
                                 eth::rpc::RpcHttpTransportOptions opts;
                                 opts.timeout = timeout;
                                 return std::make_unique<eth::rpc::RpcHttpTransport>( url, opts );
                             };

        int32_t total_weight   = 0;
        int32_t success_weight = 0;
        size_t  tried          = 0;

        for ( const auto &ep : endpoints )
        {
            total_weight += ep.consensus_weight;

            auto transport = factory( ep.url, kTimeout );

            eth::Hash256 tx_hash_parsed{};
            if ( !rlp::base::parse::hex_array( source_reference, tx_hash_parsed ) )
            {
                logger->error( "VerifyPublicChainSmartContract failed to parse source reference {}",
                               PreviewValue( source_reference ) );
                ++tried;
                continue;
            }

            const auto request  = eth::rpc::make_get_transaction_receipt_request( tx_hash_parsed, 1 );
            const auto response = transport->call( request );
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

            bool log_matched = false;
            if ( ep.bridge_contract_address.empty()
                 || mint_input.output_idx_ >= receipt->receipt.logs.size() )
            {
                ++tried;
                continue;
            }

            const auto &log_entry = receipt->receipt.logs[mint_input.output_idx_];
            const std::string log_addr_hex = rlp::base::parse::hex_array_string( log_entry.address );
            if ( log_addr_hex == ep.bridge_contract_address && !log_entry.topics.empty() )
            {
                const std::string log_topic0 =
                    rlp::base::parse::hex_array_string( log_entry.topics.front() );
                const bool accepted_topic =
                    std::find( ep.accepted_topic0_hashes.begin(),
                               ep.accepted_topic0_hashes.end(),
                               log_topic0 ) != ep.accepted_topic0_hashes.end();

                std::string event_signature;
                if ( log_entry.topics.front() == eth::abi::event_signature_hash( std::string( kBridgeSourceBurnedSig ) ) )
                {
                    event_signature = std::string( kBridgeSourceBurnedSig );
                }
                else if ( log_entry.topics.front()
                          == eth::abi::event_signature_hash( std::string( kBridgeOutInitiatedSig ) ) )
                {
                    event_signature = std::string( kBridgeOutInitiatedSig );
                }

                if ( accepted_topic && !event_signature.empty() )
                {
                    const auto params = eth::cli::event_registry().params_for( event_signature );
                    const auto decoded = eth::abi::decode_log( log_entry, event_signature, params );
                    if ( decoded.has_value() && decoded.value().size() > 3
                         && std::holds_alternative<intx::uint256>( decoded.value()[3] ) )
                    {
                        const auto burn = BridgeRelayer::ParseBurnEventValues( decoded.value() );
                        const auto decoded_chain = std::get<intx::uint256>( decoded.value()[3] );
                        if ( burn.has_value() )
                        {
                            const auto &burn_value = burn.value();
                            log_matched = decoded_chain == intx::uint256( expected_chain_id )
                                       && burn_value.token_id == mint_tx->GetTokenID()
                                       && burn_value.amount == mint_tx->GetAmount()
                                       && burn_value.amount == mint_output.encrypted_amount
                                       && burn_value.token_id == mint_output.token_id
                                       && burn_value.destination == mint_output.dest_address;
                        }
                    }
                }
            }

            if ( !log_matched )
            {
                logger->debug( "VerifyPublicChainSmartContract indexed log mismatch bridge={} tx={} index={} url={}",
                               ep.bridge_contract_address,
                               PreviewValue( source_reference ),
                               mint_input.output_idx_,
                               ep.url );
                ++tried;
                continue;
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
