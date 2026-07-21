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
#include <eth/rpc_http_transport.hpp>

#include <boost/json.hpp>

#include "base/parse_utility.hpp"
#include "eth/abi_decoder.hpp"
#include "base/logger.hpp"
#include "account/InputValidators.hpp"

#include <chrono>
#include <functional>

namespace sgns
{
    void ChainRpcEndpointProvider::AddObserver( IBridgeInitObserver &observer )
    {
        observers_.push_back( &observer );
    }

    void ChainRpcEndpointProvider::AddObserverCallback( ObserverCallback observer )
    {
        observer_callbacks_.push_back( std::move( observer ) );
    }

    bool ChainRpcEndpointProvider::Initialize( const std::filesystem::path &bridge_chains_config_path,
                                               PublicChainInputValidator   &validator,
                                               CancelChecker                is_cancelled )
    {
        auto logger = base::createLogger( "ChainRpcEndpointProvider" );

        static constexpr uint8_t kPublicEndpointWeight = 25;

        std::vector<ChainContractPair> discovered_chains;
        std::vector<uint64_t>          configured_chain_ids;

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
                logger->warn( "ChainRpcEndpointProvider: cannot open {}", bridge_chains_config_path.string() );
                return false;
            }

            std::string json_text{ std::istreambuf_iterator<char>( file ), std::istreambuf_iterator<char>() };
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

                // Optional creation_block (0 = unknown — discover at build/deploy time)
                uint64_t creation_block = 0ull;
                auto     creation_it    = chain_obj.find( "creation_block" );
                if ( creation_it != chain_obj.end() )
                {
                    creation_block = boost::json::value_to<uint64_t>( creation_it->value() );
                }

                configured_chain_ids.push_back( chain_id );

                discovered_chains.push_back( { std::string( key ), std::move( contract_addr ), chain_id, creation_block } );

                logger->info( "ChainRpcEndpointProvider: chain {} (id={}) bridge={} topic0_v1={} topic0_v2={}",
                              std::string( key ),
                              chain_id,
                              contract_addr,
                              topic0_hex_v1,
                              topic0_hex_v2 );
            }
        }
        catch ( const std::exception &e )
        {
            // T-05.1-01: malformed JSON — graceful degradation
            logger->warn( "ChainRpcEndpointProvider: failed to parse bridge_chains_config.json: {}", e.what() );
            return false;
        }

        if ( discovered_chains.empty() )
        {
            logger->warn( "ChainRpcEndpointProvider: no bridge-configured chains found" );
            return false;
        }

        // ── Runtime-fetch RPC URLs from chainid.network ─────────────────
        // Option 3: discover public RPC endpoints at startup by fetching the
        // chainlist dataset (default https://chainid.network/chains.json), parsing
        // it, and filtering to the configured chain IDs. This is a network
        // dependency on the startup path — on failure no endpoints are wired
        // (validation/backfill fail closed; relayer watch still registers).
        auto fetcher = chainlist_fetcher_ ? chainlist_fetcher_
                                          : ChainlistFetcher{ []() -> std::optional<std::string>
                                                              {
                                                                  eth::rpc::RpcHttpTransportOptions opts;
                                                                  opts.timeout = std::chrono::seconds( 15 );
                                                                  return eth::rpc::RpcHttpTransport::HttpsGet(
                                                                      "https://chainid.network/chains.json",
                                                                      opts );
                                                              } };

        std::unordered_map<uint64_t, std::vector<std::string>> rpc_urls_by_chain;
        if ( auto chainlist_text = fetcher() )
        {
            auto parsed_endpoints = eth::rpc::load_chainlist_from_json_text( *chainlist_text );
            if ( parsed_endpoints.has_value() )
            {
                auto filtered = eth::rpc::filter_to_configured_chains( std::move( parsed_endpoints.value() ),
                                                                       configured_chain_ids );
                for ( auto &ep : filtered )
                {
                    rpc_urls_by_chain[ep.chain_id].push_back( std::move( ep.url_template ) );
                }
                logger->info( "ChainRpcEndpointProvider: chainlist fetch yielded {} endpoint(s) "
                              "across {} configured chain(s)",
                              filtered.size(),
                              configured_chain_ids.size() );
            }
            else
            {
                logger->warn( "ChainRpcEndpointProvider: chainlist fetch parse failed — "
                              "no RPC endpoints wired" );
            }
        }
        else
        {
            logger->warn( "ChainRpcEndpointProvider: chainlist fetch failed — no RPC endpoints wired "
                          "(validation/backfill will fail closed; relayer watch still registers)" );
        }

        // The chainlist fetch above can block ~15s. Recheck cancellation AFTER it
        // and BEFORE publishing anything: if the account was switched mid-fetch,
        // this init is stale — its validator is about to be destroyed and must
        // not be registered (raw pointer in the global IInputValidator registry)
        // nor observers notified. This closes the post-generation-check window.
        if ( is_cancelled && is_cancelled() )
        {
            logger->info( "ChainRpcEndpointProvider: initialization cancelled (account switched "
                          "during fetch) — no endpoints registered, observers not notified" );
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
                    wrep.consensus_weight        = kPublicEndpointWeight;
                    wrep.bridge_contract_address = dc.contract_address;
                    wrep.accepted_topic0_hashes  = accepted_topic0_hashes;
                    endpoints.push_back( std::move( wrep ) );
                }
            }

            endpoints_by_chain[dc.chain_id] = std::move( endpoints );

            // Register validator for this chain. RegisterForChain records the
            // chain id so the validator self-removes from the global registry on
            // destruction (compare-and-remove) — the registry then never holds a
            // dangling pointer after this (possibly stale) manager is released.
            if ( !validator.RegisterForChain( std::to_string( dc.chain_id ) ) )
            {
                logger->warn( "ChainRpcEndpointProvider: chain_id={} already has a registered validator; "
                              "leaving the current owner unchanged",
                              dc.chain_id );
            }
        }

        for ( auto &[chain_id, endpoints] : endpoints_by_chain )
        {
            // Merge (not replace) with any existing endpoints: an operator may
            // have supplied private/API-key endpoints via
            // GeniusNode::ConfigureRpcEndpoint while this async fetch was in
            // flight. SetRpcEndpoints is a wholesale replace and would silently
            // drop them, so AddRpcEndpoints (URL-deduped) preserves them.
            const auto fetched = endpoints.size();
            validator.AddRpcEndpoints( std::to_string( chain_id ), std::move( endpoints ) );
            logger->info( "ChainRpcEndpointProvider: merged {} fetched endpoint(s) for chain_id={}",
                          fetched,
                          chain_id );
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
            for ( const auto &observer : observer_callbacks_ )
            {
                observer( discovered_chains );
            }
        }

        return any_wired;
    }
}
