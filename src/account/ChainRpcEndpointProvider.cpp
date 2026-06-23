/**
 * @file       ChainRpcEndpointProvider.cpp
 * @brief      Implementation of the ChainList RPC endpoint loading and validator wiring.
 * @date       2026-05-27
 * @author     SuperGenius
 */
#include "account/ChainRpcEndpointProvider.hpp"
#include "account/BridgeEventTypes.hpp"

#include <fstream>
#include <iterator>
#include <unordered_map>
#include <vector>

#include <eth/chainlist_provider.hpp>

#include <boost/json.hpp>

#include "base/parse_utility.hpp"
#include "eth/abi_decoder.hpp"
#include "base/logger.hpp"
#include "account/InputValidators.hpp"

namespace sgns
{
    void ChainRpcEndpointProvider::AddObserver( IBridgeInitObserver &observer )
    {
        observers_.push_back( &observer );
    }

    bool ChainRpcEndpointProvider::Initialize( const std::filesystem::path    &bridge_chains_config_path,
                                               PublicChainInputValidator       &validator )
    {
        auto logger = base::createLogger( "ChainRpcEndpointProvider" );

        // Each operator-provided RPC URL (the optional "rpc" array in
        // bridge_chains_config.json) contributes 50% consensus weight; ≥2 URLs
        // per chain reach the 75-weight verification quorum.
        static constexpr uint8_t kConfigEndpointWeight = 50;

        std::vector<ChainContractPair>                                       discovered_chains;
        std::vector<uint64_t>                                                configured_chain_ids;
        std::unordered_map<uint64_t, std::vector<std::string>>               rpc_urls_by_chain;

        // ── Compute accepted topic0 hashes: BOTH v1 (BridgeSourceBurned) and
        //    v2 (BridgeOutInitiated). The relayer and catch-up scan mint from
        //    either version, so witness validation must accept both — otherwise
        //    mints created from v2 burns are rejected by the receipt-log gate.
        auto        topic0_hash_v1 = eth::abi::event_signature_hash( std::string( kBridgeSourceBurnedSig ) );
        std::string topic0_hex_v1  = rlp::base::parse::hex_bytes( topic0_hash_v1.data(), topic0_hash_v1.size() );
        auto        topic0_hash_v2 = eth::abi::event_signature_hash( std::string( kBridgeOutInitiatedSig ) );
        std::string topic0_hex_v2  = rlp::base::parse::hex_bytes( topic0_hash_v2.data(), topic0_hash_v2.size() );
        const std::vector<std::string> accepted_topic0_hashes{ topic0_hex_v1, topic0_hex_v2 };

        // ── Read and parse bridge_chains_config.json ─────────────────────
        try
        {
            std::ifstream file( bridge_chains_config_path, std::ios::binary );
            if ( !file.is_open() )
            {
                logger->warn( "ChainRpcEndpointProvider: cannot open {}",
                              bridge_chains_config_path.string() );
                return false;
            }

            std::string json_text( ( std::istreambuf_iterator<char>( file ) ),
                                   std::istreambuf_iterator<char>() );
            file.close();

            if ( json_text.empty() )
            {
                logger->warn( "ChainRpcEndpointProvider: bridge_chains_config.json is empty at {}",
                              bridge_chains_config_path.string() );
                return false;
            }

            auto parsed = boost::json::parse( json_text );
            auto obj    = parsed.as_object();

            for ( const auto &[key, value] : obj )
            {
                // Skip metadata entries (prefixed with '_')
                if ( key.starts_with( "_" ) )
                {
                    continue;
                }

                auto chain_obj = value.as_object();

                // D-04: require numeric chain_id
                auto chain_id_it = chain_obj.find( "chain_id" );
                if ( chain_id_it == chain_obj.end() )
                {
                    logger->warn( "ChainRpcEndpointProvider: chain '{}' missing chain_id — skipping",
                                  std::string( key ) );
                    continue;
                }
                uint64_t chain_id = boost::json::value_to<uint64_t>( chain_id_it->value() );

                // D-02: require bridge_contract_address
                auto bridge_it = chain_obj.find( "bridge_contract_address" );
                if ( bridge_it == chain_obj.end() )
                {
                    logger->warn( "ChainRpcEndpointProvider: chain '{}' missing bridge_contract_address — skipping",
                                  std::string( key ) );
                    continue;
                }
                std::string contract_addr = boost::json::value_to<std::string>( bridge_it->value() );

                // Normalize to lowercase: receipts reconstruct the log address via
                // hex_array_string() (always lowercase), and the verifier compares
                // byte-for-byte. EIP-55 mixed-case config values would otherwise
                // never match, failing witness validation for valid receipts.
                contract_addr = rlp::base::parse::ascii_lower( std::move( contract_addr ) );

                // Optional "rpc" array: operator-provided public RPC URLs used
                // for receipt verification + catch-up scan. Absent/empty is
                // legal (chain still registers for relayer watch), but then
                // validation/backfill fail closed (no quorum, no URL).
                std::vector<std::string> rpc_urls;
                auto                     rpc_it = chain_obj.find( "rpc" );
                if ( rpc_it != chain_obj.end() && rpc_it->value().is_array() )
                {
                    for ( const auto &rpc_val : rpc_it->value().as_array() )
                    {
                        std::string url = boost::json::value_to<std::string>( rpc_val );
                        if ( !url.empty() )
                        {
                            rpc_urls.push_back( std::move( url ) );
                        }
                    }
                }
                rpc_urls_by_chain[chain_id] = std::move( rpc_urls );

                configured_chain_ids.push_back( chain_id );

                discovered_chains.push_back(
                    { std::string( key ), std::move( contract_addr ), chain_id } );

                logger->info( "ChainRpcEndpointProvider: chain {} (id={}) bridge={} rpc_count={} topic0_v1={} topic0_v2={}",
                              std::string( key ), chain_id,
                              discovered_chains.back().contract_address,
                              rpc_urls_by_chain[chain_id].size(),
                              topic0_hex_v1, topic0_hex_v2 );
            }
        }
        catch ( const std::exception &e )
        {
            // T-05.1-01: malformed JSON — graceful degradation
            logger->warn( "ChainRpcEndpointProvider: failed to parse bridge_chains_config.json: {}",
                          e.what() );
            return false;
        }

        // ── Wire RPC endpoints for discovered chains ─────────────────────
        std::unordered_map<uint64_t, std::vector<WeightedRpcEndpoint>> endpoints_by_chain;

        for ( const auto &dc : discovered_chains )
        {
            std::vector<WeightedRpcEndpoint> endpoints;

            auto urls_it = rpc_urls_by_chain.find( dc.chain_id );
            if ( urls_it != rpc_urls_by_chain.end() )
            {
                for ( const auto &url : urls_it->second )
                {
                    WeightedRpcEndpoint wrep;
                    wrep.url                     = url;
                    wrep.consensus_weight        = kConfigEndpointWeight;
                    wrep.bridge_contract_address = dc.contract_address;
                    wrep.accepted_topic0_hashes  = accepted_topic0_hashes;
                    endpoints.push_back( std::move( wrep ) );
                }
            }
            // No "rpc" array → empty endpoint list: the chain is still registered
            // for relayer watch (via the observer) but VerifyPublicChainSmartContract
            // and PerformStartupCatchupScan fail closed (no quorum / no URL).

            endpoints_by_chain[dc.chain_id] = std::move( endpoints );

            // D-02: Register validator for this chain
            IInputValidator::Register( std::to_string( dc.chain_id ), &validator );
        }

        for ( auto &[chain_id, endpoints] : endpoints_by_chain )
        {
            const auto count = endpoints.size();
            validator.SetRpcEndpoints( std::to_string( chain_id ), std::move( endpoints ) );
            logger->info( "ChainRpcEndpointProvider: wired {} RPC endpoints for chain_id={}",
                          count, chain_id );
        }

        // ── Return value pinned to accepted chains ───────────────────────
        const bool any_wired = !discovered_chains.empty();

        if ( !any_wired )
        {
            logger->warn( "ChainRpcEndpointProvider: no bridge-configured chains found" );
        }
        else
        {
            // D-03: notify observers when initialization succeeded
            for ( auto *observer : observers_ )
            {
                observer->OnRpcEndpointsReady( discovered_chains );
            }
        }

        return any_wired;
    }
} // namespace sgns