/**
 * @file       bridge_catchup_watcher.cpp
 * @brief      Implementation of the bridge catch-up scan watcher
 * @date       2026-07-12
 * @author     SuperGenius (ken@gnus.ai)
 * Copyright 2026 Genius Ventures, Inc.
 * SPDX-License-Identifier: MIT
 */

#include <watcher/impl/bridge_catchup_watcher.hpp>

#include <account/BridgeEventTypes.hpp>
#include <base/parse_utility.hpp>
#include <base/rlp-logger.hpp>
#include <eth/abi_decoder.hpp>
#include <eth/eth_watch_cli.hpp>
#include <eth/json_rpc.hpp>
#include <eth/rpc_http_transport.hpp>

#include <boost/chrono.hpp>
#include <boost/thread.hpp>

#include <algorithm>
#include <limits>
#include <set>
#include <tuple>
#include <unordered_map>

namespace sgns::evmwatcher
{

    BridgeCatchupWatcher::BridgeCatchupWatcher( const Config   &config,
                                                MessageCallback message_callback,
                                                ChainsProvider  chains_provider,
                                                RpcUrlResolver  rpc_resolver,
                                                BurnProcessor   burn_processor ) :
        watcher::MessagingWatcher( std::move( message_callback ) ),
        config_( config ),
        chains_provider_( std::move( chains_provider ) ),
        rpc_resolver_( std::move( rpc_resolver ) ),
        burn_processor_( std::move( burn_processor ) )
    {
    }

    void BridgeCatchupWatcher::startWatching()
    {
        auto logger = rlp::base::createLogger( "bridge_catchup_watcher" );
        logger->info( "BridgeCatchupWatcher starting: poll_interval={}s, start_block={}",
                      config_.poll_interval.count(),
                      config_.start_block );
        watcher::MessagingWatcher::startWatching();
    }

    void BridgeCatchupWatcher::stopWatching()
    {
        auto logger = rlp::base::createLogger( "bridge_catchup_watcher" );
        logger->info( "BridgeCatchupWatcher stopping" );
        watcher::MessagingWatcher::stopWatching();
    }

    uint64_t BridgeCatchupWatcher::GetLastProcessedBlock( uint64_t chain_id ) const noexcept
    {
        std::lock_guard lock( mutex_ );
        auto            it = last_block_per_chain_.find( chain_id );
        return ( it != last_block_per_chain_.end() ) ? it->second : 0ULL;
    }

    void BridgeCatchupWatcher::watch()
    {
        while ( running.load() )
        {
            poll_once();
            if ( !running.load() )
            {
                break;
            }
            boost::this_thread::sleep_for( boost::chrono::seconds( config_.poll_interval.count() ) );
        }
    }

    void BridgeCatchupWatcher::poll_once()
    {
        auto logger = rlp::base::createLogger( "bridge_catchup_watcher" );
        auto stop_requested = [this]() { return !running.load(); };

        if ( stop_requested() )
        {
            return;
        }

        // ── Compute topic0 hashes (v1 + v2) ──────────────────────────────
        static const std::string kEventSigV1( kBridgeSourceBurnedSig );
        static const std::string kEventSigV2( kBridgeOutInitiatedSig );

        const auto  topic0_hash_v1 = eth::abi::event_signature_hash( kEventSigV1 );
        std::string topic0_hex_v1  = rlp::base::parse::hex_bytes( topic0_hash_v1.data(), topic0_hash_v1.size() );

        const auto  topic0_hash_v2 = eth::abi::event_signature_hash( kEventSigV2 );
        std::string topic0_hex_v2  = rlp::base::parse::hex_bytes( topic0_hash_v2.data(), topic0_hash_v2.size() );

        // ── Snapshot current chains ──────────────────────────────────────
        const std::vector<ChainContractPair> chains = chains_provider_();
        if ( stop_requested() || chains.empty() )
        {
            return;
        }

        size_t total_backfilled = 0;
        size_t total_skipped    = 0;
        size_t chains_scanned   = 0;

        for ( const auto &chain_entry : chains )
        {
            if ( stop_requested() )
            {
                break;
            }

            const std::string chain_id_str = std::to_string( chain_entry.chain_id );

            auto rpc_url = rpc_resolver_( chain_id_str );
            if ( stop_requested() )
            {
                break;
            }
            if ( !rpc_url.has_value() )
            {
                logger->debug( "CatchUpScan: no RPC endpoint for chain {} (id={}) — skipping",
                               chain_entry.chain_name,
                               chain_entry.chain_id );
                continue;
            }

            // RPC transport — factory-injected when set, otherwise default RpcHttpTransport
            std::unique_ptr<eth::rpc::JsonRpcTransport> transport;
            if ( config_.transport_factory )
            {
                transport = config_.transport_factory( *rpc_url );
            }
            else
            {
                eth::rpc::RpcHttpTransportOptions opts;
                opts.timeout = std::chrono::seconds( 10 );
                transport = std::make_unique<eth::rpc::RpcHttpTransport>( *rpc_url, opts );
            }

            if ( stop_requested() )
            {
                break;
            }

            ++chains_scanned;

            // Parse contract address
            rlp::Address contract_addr{};
            if ( !rlp::base::parse::hex_array( chain_entry.contract_address, contract_addr ) )
            {
                logger->warn( "CatchUpScan: invalid bridge address {} for chain {}",
                              chain_entry.contract_address,
                              chain_entry.chain_name );
                continue;
            }

            // Parse topic0 hashes to Hash256 for EventFilter
            rlp::Hash256 topic0_hash256_v1{};
            if ( !rlp::base::parse::hex_array( topic0_hex_v1, topic0_hash256_v1 ) )
            {
                logger->warn( "CatchUpScan: invalid v1 topic0 for chain {}", chain_entry.chain_name );
                continue;
            }

            rlp::Hash256 topic0_hash256_v2{};
            const bool   has_v2_topic0 = rlp::base::parse::hex_array( topic0_hex_v2, topic0_hash256_v2 );

            // ── Query current block number ────────────────────────────────
            constexpr uint64_t kBlockNumberRequestId = 99;
            auto               block_number_req      = eth::rpc::make_get_block_by_number_request(
                eth::rpc::RpcBlockTag::kLatest, kBlockNumberRequestId );
            auto               block_number_resp     = transport->call( block_number_req );
            uint64_t           current_block         = 0;

            if ( stop_requested() )
            {
                break;
            }

            if ( block_number_resp.has_value() )
            {
                auto parsed_block = eth::rpc::parse_block_number_response( *block_number_resp );
                if ( parsed_block.has_value() )
                {
                    current_block = *parsed_block;
                }
            }

            if ( current_block == 0 )
            {
                logger->warn( "CatchUpScan: failed to query block number for chain {} — skipping",
                              chain_entry.chain_name );
                continue;
            }

            // ── Compute block range ──────────────────────────────────────
            uint64_t from_block = 0;
            {
                std::lock_guard lock( mutex_ );
                auto            it = last_block_per_chain_.find( chain_entry.chain_id );
                if ( it != last_block_per_chain_.end() && it->second > 0 )
                {
                    // Subsequent poll: continue from last processed block
                    from_block = it->second;
                }
                else
                {
                    // First poll: floor = max(config start_block, contract creation block).
                    // When creation_block is known (populated at build/deploy via
                    // find_contract_creation_blocks), scanning skips pre-deployment blocks.
                    // When 0 (unknown), falls back to config_.start_block as before.
                    from_block = std::max( config_.start_block, chain_entry.creation_block );
                }
            }

            if ( from_block > current_block )
            {
                continue; // Nothing new to scan
            }

            const uint64_t to_block = current_block;

            // ── Build v1 EventFilter (reused across chunks) ──────────────
            eth::EventFilter filter_v1;
            filter_v1.addresses.push_back( contract_addr );
            filter_v1.topics.push_back( topic0_hash256_v1 );

            // ── Build v2 EventFilter if applicable ────────────────────────
            eth::EventFilter filter_v2;
            if ( has_v2_topic0 )
            {
                filter_v2.addresses.push_back( contract_addr );
                filter_v2.topics.push_back( topic0_hash256_v2 );
            }

            // Receipt-local identity must be derived from the full ordered receipt.
            // This cache lives for one poll attempt only, so a failed poll always
            // performs fresh receipt requests when the same chunk is retried.
            std::unordered_map<std::string, std::optional<eth::ReceiptResult>> receipt_cache;
            std::set<std::pair<std::string, uint32_t>> seen_burns;
            uint64_t receipt_request_id = 1000000;

            // ── Forward chunked scan from from_block → current_block ──────
            // eth_getLogs with topic filter returns only matching events.
            // Scanning forward (chronological order) ensures burns are minted
            // in nonce/timestamp sequence.  max_chunks caps per-poll depth
            // (0 = unlimited — production scans all the way to current).
            constexpr uint64_t kChunkRequestIdBase = 1;
            uint64_t           chunk_request_id    = kChunkRequestIdBase;
            uint64_t           chunks_done         = 0;
            const uint64_t     max_chunks          = ( config_.max_chunks > 0 )
                                                      ? config_.max_chunks
                                                      : std::numeric_limits<uint64_t>::max();

            while ( from_block <= to_block && chunks_done < max_chunks )
            {
                if ( stop_requested() )
                {
                    break;
                }

                const uint64_t chunk_to = std::min( from_block + config_.max_blocks_per_query - 1, to_block );

                logger->info( "CatchUpScan: scanning chain {} chunk {}-{} (current={})",
                              chain_entry.chain_name,
                              from_block,
                              chunk_to,
                              current_block );

                bool chunk_cancelled = false;

                struct StagedBurn
                {
                    std::vector<eth::abi::AbiValue> decoded_values;
                    std::string                     tx_hash_hex;
                    std::string                     chain_id;
                    uint32_t                        receipt_log_index = 0;
                    bool                            is_v2 = false;
                };

                enum class ProcessLogsResult
                {
                    kSuccess,
                    kFailure,
                    kCancelled,
                };

                std::vector<StagedBurn> staged_burns;
                std::set<std::pair<std::string, uint32_t>> chunk_seen_burns;

                // Resolve and decode logs without exposing any candidate. The
                // owning chunk publishes staged_burns only after every enabled
                // query family has succeeded.
                auto process_logs = [&]( const std::vector<eth::rpc::RpcLog> &rpc_logs,
                                         bool                                  is_v2 ) -> ProcessLogsResult
                {
                    for ( const auto &rpc_log : rpc_logs )
                    {
                        if ( stop_requested() )
                        {
                            return ProcessLogsResult::kCancelled;
                        }

                        std::string tx_hash_hex = rlp::base::parse::hex_bytes(
                            rpc_log.tx_hash.data(), rpc_log.tx_hash.size() );
                        if ( tx_hash_hex.rfind( "0x", 0 ) == 0 )
                        {
                            tx_hash_hex.erase( 0, 2 );
                        }

                        auto receipt_it = receipt_cache.find( tx_hash_hex );
                        if ( receipt_it == receipt_cache.end() )
                        {
                            const auto request = eth::rpc::make_get_transaction_receipt_request(
                                rpc_log.tx_hash, receipt_request_id++ );
                            const auto response = transport->call( request );
                            std::optional<eth::ReceiptResult> parsed_receipt;
                            if ( response.has_value() )
                            {
                                parsed_receipt = eth::rpc::parse_transaction_receipt_response( *response );
                            }
                            receipt_it = receipt_cache.emplace(
                                tx_hash_hex, std::move( parsed_receipt ) ).first;
                        }

                        const auto &receipt = receipt_it->second;
                        if ( !receipt.has_value()
                             || receipt->tx_hash != rpc_log.tx_hash
                             || receipt->block_hash != rpc_log.block_hash
                             || receipt->block_number != rpc_log.block_number
                             || receipt->log_indices.size() != receipt->receipt.logs.size() )
                        {
                            logger->warn(
                                "CatchUpScan: missing or inconsistent receipt for tx {} — failing chunk",
                                tx_hash_hex );
                            return ProcessLogsResult::kFailure;
                        }

                        const auto first = std::find( receipt->log_indices.begin(),
                                                      receipt->log_indices.end(),
                                                      rpc_log.log_index );
                        if ( first == receipt->log_indices.end()
                             || std::find( std::next( first ),
                                           receipt->log_indices.end(),
                                           rpc_log.log_index )
                                    != receipt->log_indices.end() )
                        {
                            logger->warn(
                                "CatchUpScan: block-wide log index {} is unresolved in receipt {} — failing chunk",
                                rpc_log.log_index,
                                tx_hash_hex );
                            return ProcessLogsResult::kFailure;
                        }

                        const auto receipt_distance = std::distance(
                            receipt->log_indices.begin(), first );
                        if ( receipt_distance < 0 )
                        {
                            logger->warn(
                                "CatchUpScan: negative receipt-local index for tx {} — failing chunk",
                                tx_hash_hex );
                            return ProcessLogsResult::kFailure;
                        }
                        const auto receipt_log_index = eth::checked_receipt_log_ordinal(
                            static_cast<uint64_t>( receipt_distance ) );
                        if ( !receipt_log_index.has_value() )
                        {
                            logger->warn(
                                "CatchUpScan: receipt-local index overflow for tx {} — failing chunk",
                                tx_hash_hex );
                            return ProcessLogsResult::kFailure;
                        }

                        const auto burn_id = std::make_pair( tx_hash_hex, *receipt_log_index );
                        if ( seen_burns.count( burn_id ) != 0
                             || !chunk_seen_burns.emplace( burn_id ).second )
                        {
                            ++total_skipped;
                            logger->debug(
                                "CatchUpScan: burn {}:{} already seen this poll — skipping",
                                tx_hash_hex,
                                *receipt_log_index );
                            continue;
                        }

                        const std::string &event_sig = is_v2 ? kEventSigV2 : kEventSigV1;
                        const auto all_params = eth::cli::event_registry().params_for( event_sig );
                        auto decoded = eth::abi::decode_log(
                            rpc_log.log, event_sig, all_params );
                        if ( !decoded.has_value() )
                        {
                            logger->warn(
                                "CatchUpScan: failed to decode log for tx {} — failing chunk",
                                tx_hash_hex );
                            return ProcessLogsResult::kFailure;
                        }

                        staged_burns.push_back( {
                            std::move( decoded.value() ),
                            tx_hash_hex,
                            chain_id_str,
                            *receipt_log_index,
                            is_v2,
                        } );
                    }
                    return ProcessLogsResult::kSuccess;
                };

                bool v1_ok = false;
                bool v2_ok = !has_v2_topic0;

                // ── v1 query per chunk ───────────────────────────────────
                auto v1_request  = eth::rpc::make_get_logs_request( filter_v1,
                                                                     from_block,
                                                                     chunk_to,
                                                                     chunk_request_id++ );
                auto v1_response = transport->call( v1_request );

                if ( stop_requested() )
                {
                    chunk_cancelled = true;
                    break;
                }

                if ( !v1_response.has_value() )
                {
                    logger->warn( "CatchUpScan: v1 RPC call failed for chain {} (timeout/refused)",
                                  chain_entry.chain_name );
                }
                else
                {
                    auto v1_logs = eth::rpc::parse_get_logs_response( *v1_response );
                    if ( !v1_logs.has_value() )
                    {
                        logger->warn( "CatchUpScan: failed to parse v1 getLogs response for chain {} — "
                                      "response: {}",
                                      chain_entry.chain_name,
                                      v1_response->substr( 0, 500 ) );
                    }
                    else
                    {
                        const auto result = process_logs( v1_logs.value(), /*is_v2=*/false );
                        if ( result == ProcessLogsResult::kSuccess )
                        {
                            v1_ok = true;
                        }
                        else if ( result == ProcessLogsResult::kCancelled )
                        {
                            chunk_cancelled = true;
                        }
                    }
                }

                if ( chunk_cancelled )
                {
                    break;
                }

                // ── v2 query per chunk ───────────────────────────────────
                if ( has_v2_topic0 )
                {
                    if ( stop_requested() )
                    {
                        chunk_cancelled = true;
                        break;
                    }

                    auto v2_request  = eth::rpc::make_get_logs_request( filter_v2,
                                                                         from_block,
                                                                         chunk_to,
                                                                         chunk_request_id++ );
                    auto v2_response = transport->call( v2_request );

                    if ( stop_requested() )
                    {
                        chunk_cancelled = true;
                        break;
                    }

                    if ( !v2_response.has_value() )
                    {
                        logger->warn( "CatchUpScan: v2 RPC call failed for chain {} (timeout/refused)",
                                      chain_entry.chain_name );
                    }
                    else
                    {
                        auto v2_logs = eth::rpc::parse_get_logs_response( *v2_response );
                        if ( !v2_logs.has_value() )
                        {
                            logger->warn( "CatchUpScan: failed to parse v2 getLogs response for chain {} — "
                                          "response: {}",
                                          chain_entry.chain_name,
                                          v2_response->substr( 0, 500 ) );
                        }
                        else
                        {
                            const auto result = process_logs( v2_logs.value(), /*is_v2=*/true );
                            if ( result == ProcessLogsResult::kSuccess )
                            {
                                v2_ok = true;
                            }
                            else if ( result == ProcessLogsResult::kCancelled )
                            {
                                chunk_cancelled = true;
                            }
                        }
                    }
                }

                if ( chunk_cancelled )
                {
                    break;
                }

                if ( !v1_ok || !v2_ok )
                {
                    logger->warn( "CatchUpScan: chunk {}-{} for chain {} failed — will retry on next poll",
                                  from_block,
                                  chunk_to,
                                  chain_entry.chain_name );
                    break;
                }

                // Complete v1/v2 validation is the commit point. Only now can
                // candidates become externally visible or dedup state become
                // committed for later chunks in this poll.
                for ( auto &staged : staged_burns )
                {
                    if ( stop_requested() )
                    {
                        chunk_cancelled = true;
                        break;
                    }

                    try
                    {
                        const bool processed = burn_processor_(
                            staged.decoded_values,
                            staged.tx_hash_hex,
                            staged.chain_id,
                            staged.receipt_log_index );
                        if ( processed )
                        {
                            ++total_backfilled;
                            logger->info(
                                "CatchUpScan: backfilled historical {} burn {}:{} on chain {}",
                                staged.is_v2 ? "v2" : "v1",
                                staged.tx_hash_hex,
                                staged.receipt_log_index,
                                chain_entry.chain_name );
                        }
                        else
                        {
                            ++total_skipped;
                            logger->debug(
                                "CatchUpScan: burn processor returned false for tx {} — likely already processed",
                                staged.tx_hash_hex );
                        }
                    }
                    catch ( const std::exception &e )
                    {
                        ++total_skipped;
                        logger->debug(
                            "CatchUpScan: burn processor threw for tx {}: {} — skipping",
                            staged.tx_hash_hex,
                            e.what() );
                    }
                }

                if ( chunk_cancelled )
                {
                    break;
                }

                seen_burns.insert( chunk_seen_burns.begin(), chunk_seen_burns.end() );
                from_block = chunk_to + 1;
                ++chunks_done;

                if ( stop_requested() )
                {
                    break;
                }
            }

            // ── Update per-chain last block ──────────────────────────────
            // Only advance the cursor to the last successfully-scanned block.
            // If the loop completed (from_block > to_block), from_block already
            // equals to_block + 1. If the loop was capped by max_chunks,
            // from_block points at the next unscanned chunk's start
            // (last scanned chunk_to + 1) — do NOT skip to current_block + 1,
            // or unscanned burns would be permanently lost (CR-01).
            {
                std::lock_guard lock( mutex_ );
                last_block_per_chain_[chain_entry.chain_id] = from_block;
            }
        }

        if ( chains_scanned > 0 )
        {
            logger->info( "CatchUpScan: scanned {} chains — {} historical burns backfilled, "
                          "{} skipped (already processed)",
                          chains_scanned,
                          total_backfilled,
                          total_skipped );
        }
    }

} // namespace sgns::evmwatcher
