/**
 * @file       PublicChainInputValidator.cpp
 * @brief      Input validation strategy for public-chain source proofs
 * @date       2026-06-02
 */
#include "account/PublicChainInputValidator.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <base/parse_utility.hpp>
#include <base/rlp-logger.hpp>
#include <crypto/hasher.hpp>
#include <eth/abi_decoder.hpp>
#include <eth/eth_watch_cli.hpp>
#include <eth/json_rpc.hpp>
#include <eth/rpc_http_transport.hpp>

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

        /// Local genius chain id: the unscoped form, the empty form, or a
        /// ScopedChainId private-network form ("supergenius/<private_net>",
        /// TransactionManager::ScopedChainId). Scoped forms are matched
        /// explicitly by prefix, never by stripping the scope.
        constexpr std::string_view kLocalGeniusChainPrefix = "supergenius/";

        bool IsLocalGeniusChainId( const std::string &chain_id )
        {
            return chain_id.empty() || chain_id == "supergenius" || chain_id.rfind( kLocalGeniusChainPrefix, 0 ) == 0;
        }

        /**
         * @brief MINT-02 taxonomy: transaction types whose value is created by an
         *        external public-chain burn and therefore always require
         *        public-chain evidence (a non-empty source reference and a
         *        non-local source chain id).
         *
         * Every other type routed to this validator (escrow holds, genesis-path
         * and registration traffic on local chain ids) keeps the local path.
         * @param tx Transaction whose evidence requirements are being decided.
         * @return True exactly for the mint types ("mint", "mint-v2").
         */
        bool RequiresPublicChainEvidence( const GeniusTransaction &tx )
        {
            const auto type = tx.GetType();
            return type == "mint" || type == "mint-v2";
        }

        /// Burn facts decoded from a bridge-event receipt log.
        struct DecodedBurnFacts
        {
            TokenID     token_id;
            uint64_t    amount = 0;
            std::string recipient;
        };

        /**
         * @brief MINT-04 (finding #5): decode a receipt log that already matched
         *        the bridge contract and an accepted topic0, and bind its burn
         *        facts — token id, amount, recipient — to the mint's own
         *        outputs (Wormhole-style exact attestation-payload binding).
         *
         * The event shape is selected by the canonical topic0 (v1
         * BridgeSourceBurned or v2 BridgeOutInitiated) and decoded with the
         * registered ABI descriptors; the recipient normalization (v1 raw
         * bytes vs v2 X-only decompression) reuses BridgeRelayer's one home.
         * Every failure mode fails closed: a matched log that is not a
         * canonical bridge event, cannot be decoded, or whose facts differ
         * from the mint's own fields must never confirm the claim.
         *
         * @param mint The mint transaction whose own outputs are compared.
         * @param log The matched receipt log entry.
         * @param logger Logger for the named-rule failure diagnostics.
         * @return The decoded facts on exact match, std::nullopt on any failure.
         */
        std::optional<DecodedBurnFacts> DecodeAndBindBurnFacts( const MintTransactionV2    &mint,
                                                                const eth::codec::LogEntry &log,
                                                                const base::Logger         &logger )
        {
            // MINT-04 (CR-01): the burn facts attest exactly one output, and the
            // mint's fact accessors read that first output — a multi-output mint
            // whose output[0] matches the burn would pass every comparison below
            // while minting extra outputs. The evidence boundary rejects it (the
            // deserializer stays open for historical data; this is where the
            // burn facts authorize the output set).
            if ( mint.GetUTXOParameters().second.size() != 1 )
            {
                logger->error( "receipt/mint fact binding failed: multi-output mint tx={}",
                               PreviewValue( mint.GetHash() ) );
                return std::nullopt;
            }

            const auto canonical_topic0_hex = []( std::string_view signature )
            {
                const auto hash = eth::abi::event_signature_hash( std::string( signature ) );
                return rlp::base::parse::hex_bytes( hash.data(), hash.size() );
            };
            static const std::string kSourceBurnedTopic0 = canonical_topic0_hex( kBridgeSourceBurnedSig );
            static const std::string kOutInitiatedTopic0 = canonical_topic0_hex( kBridgeOutInitiatedSig );

            const std::string log_topic0 = rlp::base::parse::hex_array_string( log.topics.front() );
            std::string       event_signature;
            if ( log_topic0 == kSourceBurnedTopic0 )
            {
                event_signature = std::string( kBridgeSourceBurnedSig );
            }
            else if ( log_topic0 == kOutInitiatedTopic0 )
            {
                event_signature = std::string( kBridgeOutInitiatedSig );
            }
            else
            {
                logger->error( "receipt/mint fact binding failed: matched topic0 is not a canonical "
                               "bridge event topic (mint tx={})",
                               PreviewValue( mint.GetHash() ) );
                return std::nullopt;
            }

            const auto params  = eth::cli::event_registry().params_for( event_signature );
            const auto decoded = eth::abi::decode_log( log, event_signature, params );
            if ( !decoded.has_value() )
            {
                logger->error( "receipt/mint fact binding failed: matched log is undecodable as {} "
                               "(mint tx={})",
                               event_signature,
                               PreviewValue( mint.GetHash() ) );
                return std::nullopt;
            }

            const auto burn = BridgeRelayer::ParseBurnEventValues( decoded.value() );
            if ( burn.has_error() )
            {
                logger->error( "receipt/mint fact binding failed: decoded event values are malformed "
                               "(mint tx={})",
                               PreviewValue( mint.GetHash() ) );
                return std::nullopt;
            }

            const auto &facts = burn.value();
            if ( facts.token_id != mint.GetTokenID() )
            {
                logger->error( "receipt/mint fact binding failed: token mismatch on mint tx={}",
                               PreviewValue( mint.GetHash() ) );
                return std::nullopt;
            }
            if ( facts.amount != mint.GetAmount() )
            {
                logger->error( "receipt/mint fact binding failed: amount mismatch on mint tx={}",
                               PreviewValue( mint.GetHash() ) );
                return std::nullopt;
            }
            if ( facts.destination != mint.GetPrimaryDestinationAddress() )
            {
                logger->error( "receipt/mint fact binding failed: recipient mismatch on mint tx={}",
                               PreviewValue( mint.GetHash() ) );
                return std::nullopt;
            }
            return DecodedBurnFacts{ facts.token_id, facts.amount, facts.destination };
        }
    } // namespace

    bool PublicChainInputValidator::ValidateUTXOParameters( const UTXOTxParameters &params,
                                                            const std::string      &address,
                                                            const UTXOManager      &utxo_manager ) const
    {
        auto logger = InputValidatorLogger();
        (void) address;
        (void) utxo_manager;
        logger->trace( "ValidateUTXOParameters(PublicChain): inputs={} outputs={}",
                       params.first.size(),
                       params.second.size() );
        // Public-chain claims are not validated against local UTXO ownership.
        // We still require input references and minted outputs to be explicit.
        const bool valid = !params.first.empty() && !params.second.empty();
        if ( valid )
        {
            logger->info( "ValidateUTXOParameters(PublicChain) accepted inputs={} outputs={}",
                          params.first.size(),
                          params.second.size() );
        }
        else
        {
            logger->debug( "ValidateUTXOParameters(PublicChain) rejected empty params" );
        }
        return valid;
    }

    IInputValidator::WitnessVerdict PublicChainInputValidator::ValidateWitness( const ConsensusSubject  &subject,
                                                                                const GeniusTransaction &tx,
                                                                                const UTXOTxParameters  &params,
                                                                                const Blockchain &blockchain ) const
    {
        auto logger = InputValidatorLogger();
        (void) blockchain;
        logger->trace( "ValidateWitness(PublicChain): tx={} inputs={} outputs={}",
                       PreviewValue( tx.GetHash() ),
                       params.first.size(),
                       params.second.size() );
        if ( params.first.empty() || params.second.empty() )
        {
            logger->error( "ValidateWitness(PublicChain) invalid inputs: inputs={} outputs={}",
                           params.first.size(),
                           params.second.size() );
            return IInputValidator::WitnessVerdict::kInvalid;
        }

        auto nonce_subject = ConsensusManager::DecodeNonceSubject( subject );
        if ( nonce_subject.has_error() )
        {
            logger->error( "ValidateWitness(PublicChain) failed to decode nonce subject for tx={}",
                           PreviewValue( tx.GetHash() ) );
            return IInputValidator::WitnessVerdict::kInvalid;
        }

        if ( !nonce_subject.value().has_utxo_commitment() )
        {
            logger->error( "ValidateWitness(PublicChain) missing UTXO commitment for tx={}",
                           PreviewValue( tx.GetHash() ) );
            return IInputValidator::WitnessVerdict::kInvalid;
        }

        const auto &commitment = nonce_subject.value().utxo_commitment();
        if ( commitment.consumed_outpoints_size() != static_cast<int>( params.first.size() ) ||
             commitment.produced_outputs_size() != static_cast<int>( params.second.size() ) )
        {
            logger->debug( "ValidateWitness(PublicChain) commitment size mismatch for tx={}",
                           PreviewValue( tx.GetHash() ) );
            return IInputValidator::WitnessVerdict::kInvalid;
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
            source_reference = tx.GetUncleHash();
        }

        const auto evidence = GatherVerificationEvidence( tx, source_reference );
        const bool verified = evidence.valid;
        if ( verified )
        {
            // Bind the evidence to this exact subject so CreateVote can populate
            // the vote's slot hashes from endpoints that actually verified THIS
            // claim (issue #364). Evidence for a rejected claim is never stored.
            if ( auto claim_key = ClaimKey( subject ); claim_key.has_value() )
            {
                StoreEvidence( claim_key.value(), evidence );
            }
            else
            {
                logger->warn( "ValidateWitness(PublicChain) could not derive claim key for tx={}; "
                              "vote will abstain from all RPC slots",
                              PreviewValue( tx.GetHash() ) );
            }
            logger->info( "ValidateWitness(PublicChain) succeeded for tx={} source={}",
                          PreviewValue( tx.GetHash() ),
                          PreviewValue( source_reference ) );
        }
        else
        {
            logger->error( "ValidateWitness(PublicChain) failed for tx={} source={}",
                           PreviewValue( tx.GetHash() ),
                           PreviewValue( source_reference ) );
        }
        return verified ? IInputValidator::WitnessVerdict::kValid : IInputValidator::WitnessVerdict::kInvalid;
    }

    void PublicChainInputValidator::SetRpcEndpoints( const std::string               &chain_id,
                                                     std::vector<WeightedRpcEndpoint> endpoints )
    {
        auto logger              = InputValidatorLogger();
        rpc_endpoints_[chain_id] = std::move( endpoints );
        logger->info( "SetRpcEndpoints: chain_id={} endpoint_count={}", chain_id, rpc_endpoints_[chain_id].size() );
    }

    void PublicChainInputValidator::AddRpcEndpoints( const std::string               &chain_id,
                                                     std::vector<WeightedRpcEndpoint> endpoints )
    {
        auto  logger   = InputValidatorLogger();
        auto &existing = rpc_endpoints_[chain_id];

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
                if ( std::find( cur.accepted_topic0_hashes.begin(), cur.accepted_topic0_hashes.end(), h ) ==
                     cur.accepted_topic0_hashes.end() )
                {
                    cur.accepted_topic0_hashes.push_back( h );
                    changed = true;
                }
            }
            if ( cur.bridge_contract_address.empty() && !fetched_bridge.empty() )
            {
                cur.bridge_contract_address = fetched_bridge;
                changed                     = true;
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
                      chain_id,
                      added,
                      upgraded,
                      existing.size() );
    }

    std::vector<uint8_t> RpcVerificationEvidence::SlotHash( size_t slot_index ) const
    {
        // Slots are drawn exclusively from endpoints that actually verified the
        // claim (issue #364). A configured-but-failed endpoint is simply absent
        // from the evidence, so its slot stays empty (abstain).
        switch ( slot_index )
        {
            case 0:
                return successful_direct_api.value_or( std::vector<uint8_t>{} );
            case 1:
                return successful_public.size() >= 1 ? successful_public[0] : std::vector<uint8_t>{};
            case 2:
                // Distinct from slot 1: successful_public holds URL-distinct endpoints.
                return successful_public.size() >= 2 ? successful_public[1] : std::vector<uint8_t>{};
            default:
                return {};
        }
    }

    std::optional<std::string> PublicChainInputValidator::ClaimKey( const ConsensusSubject &subject )
    {
        auto subject_id = ConsensusManager::ComputeSubjectId( subject );
        if ( subject_id.has_error() || subject_id.value().empty() )
        {
            return std::nullopt;
        }
        return subject_id.value();
    }

    void PublicChainInputValidator::StoreEvidence( const std::string      &claim_key,
                                                   RpcVerificationEvidence evidence ) const
    {
        std::lock_guard lock( evidence_mutex_ );
        if ( evidence_by_claim_.emplace( claim_key, std::move( evidence ) ).second )
        {
            evidence_order_.push_back( claim_key );
        }
        else
        {
            // Re-validation of the same claim replaces the stale evidence in place.
            evidence_by_claim_[claim_key] = std::move( evidence );
        }

        while ( evidence_order_.size() > kMaxCachedEvidence )
        {
            evidence_by_claim_.erase( evidence_order_.front() );
            evidence_order_.pop_front();
        }
    }

    std::optional<RpcVerificationEvidence> PublicChainInputValidator::TakeEvidence( const std::string &claim_key ) const
    {
        std::lock_guard lock( evidence_mutex_ );
        auto            it = evidence_by_claim_.find( claim_key );
        if ( it == evidence_by_claim_.end() )
        {
            return std::nullopt;
        }
        RpcVerificationEvidence evidence = std::move( it->second );
        evidence_by_claim_.erase( it );
        evidence_order_.erase( std::remove( evidence_order_.begin(), evidence_order_.end(), claim_key ),
                               evidence_order_.end() );
        return evidence;
    }

    bool PublicChainInputValidator::VerifyPublicChainSmartContract( const GeniusTransaction &tx,
                                                                    const std::string       &source_reference ) const
    {
        return GatherVerificationEvidence( tx, source_reference ).valid;
    }

    RpcVerificationEvidence PublicChainInputValidator::GatherVerificationEvidence(
        const GeniusTransaction &tx,
        const std::string       &source_reference ) const
    {
        auto                    logger = InputValidatorLogger();
        RpcVerificationEvidence evidence;

        // MINT-04 fact binding applies to the bridge mint type that carries the
        // burn-fact accessors. Legacy v1 mints are admission-disabled by the
        // MINT-03 plans (21-03/21-05/21-06) and keep today's receipt checks.
        const auto *mint_v2 = dynamic_cast<const MintTransactionV2 *>( &tx );

        logger->trace( "GatherVerificationEvidence: tx={} chain_id={} source={}",
                       PreviewValue( tx.GetHash() ),
                       tx.GetChainId(),
                       PreviewValue( source_reference ) );

        if ( source_reference.empty() )
        {
            if ( RequiresPublicChainEvidence( tx ) )
            {
                // MINT-02 (finding #3): a public-evidence-required mint type with
                // no source reference names no burn to verify against — the claim
                // must fail closed instead of certifying with every slot empty.
                logger->error( "GatherVerificationEvidence rejected empty source reference on "
                               "public-evidence-required mint type={} tx={}",
                               tx.GetType(),
                               PreviewValue( tx.GetHash() ) );
                return evidence;
            }
            // Nothing to verify against a public chain: the claim is accepted but
            // no endpoint confirmed anything, so every slot stays empty.
            logger->debug( "GatherVerificationEvidence skipped because source reference is empty" );
            evidence.valid = true;
            return evidence;
        }

        const auto chain_id = tx.GetChainId();
        if ( RequiresPublicChainEvidence( tx ) && IsLocalGeniusChainId( chain_id ) )
        {
            // MINT-02 (finding #3): a mint type claiming an empty or local genius
            // chain id (including ScopedChainId private-network forms) has no
            // public chain to gather evidence from — fail closed.
            logger->error( "GatherVerificationEvidence rejected local chain id '{}' on "
                           "public-evidence-required mint type={} tx={}",
                           chain_id,
                           tx.GetType(),
                           PreviewValue( tx.GetHash() ) );
            return evidence;
        }
        if ( chain_id.empty() || chain_id == "supergenius" )
        {
            logger->debug( "GatherVerificationEvidence bypassed for local chain_id={}", chain_id );
            evidence.valid = true;
            return evidence;
        }

        auto chain_it = rpc_endpoints_.find( chain_id );
        if ( chain_it == rpc_endpoints_.end() || chain_it->second.empty() )
        {
            logger->error( "GatherVerificationEvidence has no RPC endpoints for chain_id={}", chain_id );
            return evidence;
        }

        const auto &endpoints = chain_it->second;

        static constexpr int32_t kRequiredConsensusWeight  = 75;
        static constexpr uint8_t kDirectApiWeightThreshold = 50;
        static constexpr auto    kTimeout                  = std::chrono::seconds( 10 );

        // Resolve transport factory: use injected factory if set (D-07, D-14),
        // otherwise default to real RpcHttpTransport (production path per D-16).
        auto factory = transport_factory_ ? transport_factory_
                                          : []( const std::string &url, std::chrono::seconds timeout )
        {
            eth::rpc::RpcHttpTransportOptions opts;
            opts.timeout = timeout;
            return std::make_unique<eth::rpc::RpcHttpTransport>( url, opts );
        };

        const auto hash_url = []( const std::string &url )
        {
            const auto digest = crypto::sha2_256( reinterpret_cast<const uint8_t *>( url.data() ), url.size() );
            return std::vector<uint8_t>( digest.begin(), digest.end() );
        };

        int32_t total_weight = 0;
        size_t  tried        = 0;

        // Every configured endpoint is queried, with no early exit once the quorum
        // weight is reached: slot 2 requires a *second* distinct successful public
        // endpoint, and a direct endpoint may appear after the public ones. Bailing
        // out early would leave slots empty for endpoints that did confirm.
        for ( const auto &ep : endpoints )
        {
            total_weight += ep.consensus_weight;
            ++tried;

            eth::Hash256 tx_hash_parsed{};
            if ( !rlp::base::parse::hex_array( source_reference, tx_hash_parsed ) )
            {
                logger->error( "GatherVerificationEvidence failed to parse source reference {}",
                               PreviewValue( source_reference ) );
                continue;
            }

            auto       transport = factory( ep.url, kTimeout );
            const auto request   = eth::rpc::make_get_transaction_receipt_request( tx_hash_parsed, 1 );
            const auto response  = transport->call( request );
            if ( !response.has_value() )
            {
                logger->debug( "GatherVerificationEvidence RPC transport failed for url={}", ep.url );
                continue;
            }

            const auto receipt = eth::rpc::parse_transaction_receipt_response( response.value() );
            if ( !receipt.has_value() )
            {
                logger->debug( "GatherVerificationEvidence failed to parse receipt from url={}", ep.url );
                continue;
            }

            if ( !receipt->receipt.status.has_value() || !receipt->receipt.status.value() )
            {
                // A reverted source transaction invalidates the claim outright, no
                // matter what the other endpoints report. Discard any evidence
                // gathered so far so no slot can be populated for a bad claim.
                logger->error( "GatherVerificationEvidence receipt status failed for tx={} via url={}",
                               PreviewValue( source_reference ),
                               ep.url );
                return RpcVerificationEvidence{};
            }

            // MINT-04 (CR-02): an endpoint with no bridge contract or no
            // accepted topic0 set cannot check bridge evidence at all — for an
            // evidence-required mint it must not confirm on receipt existence
            // alone. Scoped to the mint types: every other routed type keeps
            // the bridge-optional confirmation. Like the topic-mismatch case
            // below, the endpoint earns no weight and no slot while quorum
            // evaluation continues over the remaining endpoints.
            if ( RequiresPublicChainEvidence( tx ) &&
                 ( ep.bridge_contract_address.empty() || ep.accepted_topic0_hashes.empty() ) )
            {
                logger->warn(
                    "GatherVerificationEvidence endpoint lacks bridge binding for chain_id={} url={} — not confirming",
                    chain_id,
                    ep.url );
                continue;
            }

            // Defense-in-depth: verify receipt logs match expected bridge contract and event topic0.
            // If bridge_contract_address is configured, at least one log must match.
            if ( !ep.bridge_contract_address.empty() )
            {
                const eth::codec::LogEntry *matched_log = nullptr;
                for ( const auto &log_entry : receipt->receipt.logs )
                {
                    std::string log_addr_hex = rlp::base::parse::hex_array_string( log_entry.address );
                    if ( log_addr_hex != ep.bridge_contract_address || log_entry.topics.empty() )
                    {
                        continue;
                    }
                    // Accept any of the configured topic0 hashes (v1 BridgeSourceBurned
                    // or v2 BridgeOutInitiated) — both event versions can back a mint.
                    const std::string log_topic0 = rlp::base::parse::hex_array_string( log_entry.topics.front() );
                    if ( std::find( ep.accepted_topic0_hashes.begin(), ep.accepted_topic0_hashes.end(), log_topic0 ) !=
                         ep.accepted_topic0_hashes.end() )
                    {
                        matched_log = &log_entry;
                        break;
                    }
                }
                if ( matched_log == nullptr )
                {
                    // Per-endpoint topic mismatch must NOT abort the whole
                    // verification. After the private/public endpoint merge, an
                    // operator's endpoint may carry only the legacy v1 topic0
                    // while fetched endpoints carry {v1, v2}; a valid v2 receipt
                    // legitimately mismatches the v1-only endpoint and must still
                    // reach quorum on the v1+v2 endpoints. Treat the mismatch as
                    // "this endpoint did not confirm" and continue — it earns no
                    // weight and no slot.
                    logger->debug( "GatherVerificationEvidence topic mismatch bridge={} tx={} url={} "
                                   "— endpoint covers {} topic0 hash(es); continuing quorum evaluation",
                                   ep.bridge_contract_address,
                                   PreviewValue( source_reference ),
                                   ep.url,
                                   ep.accepted_topic0_hashes.size() );
                    continue;
                }

                // MINT-04 (finding #5): a matched log must also carry THIS mint's
                // burn facts. An honestly-served receipt for a different burn
                // (token, amount, or recipient differing from the mint's own
                // outputs) cannot confirm the claim, and an undecodable matched
                // log fails closed (T-21-0204) — discard all evidence so no slot
                // can be populated for a mismatched mint, exactly like the
                // reverted-receipt case above.
                if ( mint_v2 != nullptr )
                {
                    const auto bound_facts = DecodeAndBindBurnFacts( *mint_v2, *matched_log, logger );
                    if ( !bound_facts.has_value() )
                    {
                        return RpcVerificationEvidence{};
                    }
                    evidence.decoded_token_id  = bound_facts->token_id;
                    evidence.decoded_amount    = bound_facts->amount;
                    evidence.decoded_recipient = bound_facts->recipient;
                }
            }

            // The endpoint passed every required check for this exact claim.
            evidence.successful_weight += ep.consensus_weight;
            if ( ep.consensus_weight >= kDirectApiWeightThreshold )
            {
                if ( !evidence.successful_direct_api.has_value() )
                {
                    evidence.successful_direct_api = hash_url( ep.url );
                }
            }
            else if ( evidence.successful_public.size() < 2 )
            {
                evidence.successful_public.push_back( hash_url( ep.url ) );
            }
        }

        evidence.valid = evidence.successful_weight >= kRequiredConsensusWeight;
        if ( evidence.valid )
        {
            logger->info( "GatherVerificationEvidence succeeded: {}/{} weight over {} endpoints for tx={} "
                          "(direct_api={} public={})",
                          evidence.successful_weight,
                          total_weight,
                          tried,
                          PreviewValue( source_reference ),
                          evidence.successful_direct_api.has_value(),
                          evidence.successful_public.size() );
        }
        else
        {
            logger->error( "GatherVerificationEvidence insufficient consensus for tx={}: {}/{} weight (need >= {})",
                           PreviewValue( source_reference ),
                           evidence.successful_weight,
                           total_weight,
                           kRequiredConsensusWeight );
        }
        return evidence;
    }
} // namespace sgns
