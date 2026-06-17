/**
 * @file       ChainRpcEndpointProvider.cpp
 * @brief      Implementation of the ChainList RPC endpoint loading and validator wiring.
 * @date       2026-05-27
 * @author     SuperGenius
 */
#include "account/ChainRpcEndpointProvider.hpp"

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

        static constexpr std::string_view kBridgeEventSignature =
            "BridgeSourceBurned(address,uint256,uint256,uint256,uint256,bytes)";
        static constexpr uint8_t kPublicEndpointWeight = 25;

        std::vector<ChainContractPair>             discovered_chains;
        std::vector<uint64_t>                      configured_chain_ids;

        // ── Compute topic0 once ──────────────────────────────────────────
        auto        topic0_hash = eth::abi::event_signature_hash( std::string( kBridgeEventSignature ) );
        std::string topic0_hex  = rlp::base::parse::hex_bytes( topic0_hash.data(), topic0_hash.size() );

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

                configured_chain_ids.push_back( chain_id );

                discovered_chains.push_back(
                    { std::string( key ), std::move( contract_addr ), chain_id } );

                logger->info( "ChainRpcEndpointProvider: chain {} (id={}) bridge={} topic0={}",
                              std::string( key ), chain_id, contract_addr, topic0_hex );
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
            WeightedRpcEndpoint wrep;
            wrep.url                     = {};
            wrep.consensus_weight       = kPublicEndpointWeight;
            wrep.bridge_contract_address = dc.contract_address;
            wrep.event_topic0           = topic0_hex;

            endpoints_by_chain[dc.chain_id].push_back( std::move( wrep ) );

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