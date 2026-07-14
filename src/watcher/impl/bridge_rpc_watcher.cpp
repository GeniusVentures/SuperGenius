/**
 * @file       bridge_rpc_watcher.cpp
 * @brief      Implementation of the bridge RPC watcher
 * @date       2026-02-03
 * @author     SuperGenius (ken@gnus.ai)
 * Copyright 2026 Genius Ventures, Inc.
 * SPDX-License-Identifier: MIT
 */

#include <watcher/impl/bridge_rpc_watcher.hpp>

#include <eth/json_rpc.hpp>
#include <eth/eth_receipt_source.hpp>
#include <eth/rpc_receipt_source.hpp>
#include <base/parse_utility.hpp>
#include <base/rlp-logger.hpp>

#include <boost/json.hpp>
#include <boost/json/serialize.hpp>
#include <boost/chrono.hpp>
#include <boost/thread.hpp>

#include <algorithm>

namespace sgns::evmwatcher
{

    BridgeRpcWatcher::BridgeRpcWatcher( const Config       &config,
                                        MessageCallback     message_callback,
                                        BridgeClaimCallback claim_callback ) :
        watcher::MessagingWatcher( std::move( message_callback ) ),
        config_( config ),
        claim_callback_( std::move( claim_callback ) ),
        transport_( config.rpc_url )
    {
    }

    void BridgeRpcWatcher::startWatching()
    {
        auto logger = rlp::base::createLogger( "bridge_rpc_watcher" );
        logger->info( "BridgeRpcWatcher starting: rpc={}, chain_id={}, contract={}, event={}",
                      config_.rpc_url,
                      config_.chain_id,
                      config_.contract_address,
                      config_.event_signature );
        watcher::MessagingWatcher::startWatching();
    }

    void BridgeRpcWatcher::stopWatching()
    {
        auto logger = rlp::base::createLogger( "bridge_rpc_watcher" );
        logger->info( "BridgeRpcWatcher stopping" );
        watcher::MessagingWatcher::stopWatching();
    }

    void BridgeRpcWatcher::watch()
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

    void BridgeRpcWatcher::poll_once()
    {
        auto logger = rlp::base::createLogger( "bridge_rpc_watcher" );

        const auto parsed_contract = rlp::base::parse::hex_bytes( config_.contract_address );
        if ( !parsed_contract.has_value() || parsed_contract->size() != 20 )
        {
            logger->error( "Invalid contract address: {}", config_.contract_address );
            return;
        }
        eth::Address contract{};
        std::copy_n( parsed_contract->begin(), 20, contract.begin() );

        const auto topic0 = eth::abi::event_signature_hash( config_.event_signature );

        // 1. Get latest block
        const auto latest_block_req  = eth::rpc::make_get_block_by_number_request( eth::rpc::RpcBlockTag::kLatest, 1 );
        const auto latest_block_resp = transport_.call( latest_block_req );
        if ( !latest_block_resp.has_value() )
        {
            logger->warn( "Failed to get latest block number" );
            return;
        }
        const auto latest_block = eth::rpc::parse_block_number_response( latest_block_resp.value() );
        if ( !latest_block.has_value() )
        {
            logger->warn( "Failed to parse block number response" );
            return;
        }

        // 2. Ensure safe block range
        const uint64_t safe_latest = ( latest_block.value() > config_.confirmation_depth )
                                         ? ( latest_block.value() - config_.confirmation_depth )
                                         : 0ULL;

        if ( last_block_ == 0 )
        {
            last_block_ = ( safe_latest > config_.max_log_range ) ? ( safe_latest - config_.max_log_range + 1 ) : 0ULL;
        }

        if ( last_block_ > safe_latest )
        {
            return;
        }

        const auto to_block = std::min( safe_latest, last_block_ + config_.max_log_range - 1 );

        // 3. Fetch logs
        eth::EventFilter filter;
        filter.addresses.push_back( contract );
        filter.topics.push_back( topic0 );

        const auto get_logs_req  = eth::rpc::make_get_logs_request( filter, last_block_, to_block, 2 );
        const auto get_logs_resp = transport_.call( get_logs_req );
        if ( !get_logs_resp.has_value() )
        {
            logger->warn( "Failed to fetch logs for blocks {}-{}", last_block_, to_block );
            return;
        }

        const auto logs = eth::rpc::parse_get_logs_response( get_logs_resp.value() );
        if ( !logs.has_value() )
        {
            logger->warn( "Failed to parse logs response for blocks {}-{}", last_block_, to_block );
            return;
        }

        // 4. For each log, fetch receipt and build BridgeEventClaim
        for ( const auto &rpc_log : logs.value() )
        {
            const auto receipt_req  = eth::rpc::make_get_transaction_receipt_request( rpc_log.tx_hash, 3 );
            const auto receipt_resp = transport_.call( receipt_req );
            if ( !receipt_resp.has_value() )
            {
                logger->warn( "Failed to fetch receipt for tx {}",
                              rlp::base::parse::hex_array_string( rpc_log.tx_hash ) );
                continue;
            }

            const auto receipt = eth::rpc::parse_transaction_receipt_response( receipt_resp.value() );
            if ( !receipt.has_value() )
            {
                logger->warn( "Failed to parse receipt for tx {}",
                              rlp::base::parse::hex_array_string( rpc_log.tx_hash ) );
                continue;
            }

            eth::BridgeEventClaim claim;
            claim.src_chain_id    = config_.chain_id;
            claim.dest_chain_id   = config_.dest_chain_id;
            claim.block_number    = receipt->block_number;
            claim.block_hash      = receipt->block_hash;
            claim.tx_hash         = receipt->tx_hash;
            claim.log_index       = rpc_log.log_index;
            claim.bridge_contract = contract;
            claim.event_topic0    = topic0;
            claim.observed_at    = static_cast<uint64_t>( std::chrono::system_clock::now().time_since_epoch().count() );
            claim.finality_depth = static_cast<uint32_t>( config_.confirmation_depth );

            if ( rpc_log.log.topics.size() >= 2 )
            {
                claim.topics.reserve( rpc_log.log.topics.size() );
                for ( const auto &t : rpc_log.log.topics )
                {
                    claim.topics.push_back( t );
                }
            }
            claim.data = rpc_log.log.data;

            // Verify the receipt log
            const auto verify_result = eth::verify_receipt_log( receipt.value(), claim );
            if ( !verify_result )
            {
                logger->warn( "Receipt log verification failed for tx {}",
                              rlp::base::parse::hex_array_string( rpc_log.tx_hash ) );
                continue;
            }

            logger->info( "Bridge event detected: tx={} block={} log_index={}",
                          rlp::base::parse::hex_array_string( rpc_log.tx_hash ),
                          rpc_log.block_number,
                          rpc_log.log_index );

            if ( claim_callback_ )
            {
                claim_callback_( claim );
            }

            if ( messageCallback )
            {
                boost::json::object msg;
                msg["type"]         = "bridge_event";
                msg["tx_hash"]      = rlp::base::parse::hex_array_string( rpc_log.tx_hash );
                msg["block_number"] = rpc_log.block_number;
                msg["log_index"]    = rpc_log.log_index;
                messageCallback( boost::json::serialize( msg ) );
            }
        }

        last_block_ = to_block + 1;
    }

} // namespace sgns::evmwatcher
