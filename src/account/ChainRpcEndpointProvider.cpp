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

namespace sgns
{
    ChainRpcEndpointProvider::ChainRpcEndpointProvider( ChainIdMap chain_id_map )
        : chain_id_map_( std::move( chain_id_map ) )
    {
    }

    bool ChainRpcEndpointProvider::Initialize( PublicChainInputValidator   &validator,
                                               const ChainRpcProviderConfig &config,
                                               const base::Logger           &logger ) const
    {
        if ( chain_id_map_.empty() )
        {
            logger->warn( "ChainRpcEndpointProvider: no chain ID mappings configured" );
            return false;
        }

        // Collect the numeric chain IDs we care about.
        std::vector<uint64_t> configured_chain_ids;
        for ( const auto &[name, chain_id] : chain_id_map_ )
        {
            (void)name;
            configured_chain_ids.push_back( chain_id );
        }

        static constexpr uint8_t kPublicEndpointWeight = 25;
        static constexpr uint8_t kDirectEndpointWeight = 50;

        std::unordered_map<uint64_t, std::vector<WeightedRpcEndpoint>> endpoints_by_chain;

        // ── Public endpoints from the ChainList provider ──────────────
        if ( !config.chains_json_path.empty() )
        {
            std::ifstream file( config.chains_json_path, std::ios::binary );
            if ( !file.is_open() )
            {
                logger->warn( "ChainRpcEndpointProvider: chains.json not found at {}",
                              config.chains_json_path.string() );
                // Continue — direct endpoints may still be available.
            }
            else
            {
                std::string json_text( ( std::istreambuf_iterator<char>( file ) ),
                                       std::istreambuf_iterator<char>() );
                file.close();

                if ( json_text.empty() )
                {
                    logger->warn( "ChainRpcEndpointProvider: chains.json is empty at {}",
                                  config.chains_json_path.string() );
                }
                else
                {
                    auto parse_result = eth::rpc::load_chainlist_from_json_text( json_text );
                    if ( !parse_result )
                    {
                        logger->warn( "ChainRpcEndpointProvider: failed to parse chains.json: {}",
                                      parse_result.error().field );
                    }
                    else
                    {
                        auto filtered = eth::rpc::filter_to_configured_chains(
                            std::move( parse_result.value() ), configured_chain_ids );

                        for ( const auto &ep : filtered )
                        {
                            WeightedRpcEndpoint wrep{ ep.url_template, kPublicEndpointWeight, {}, {} };

                            // D-02, D-05: carry bridge contract address and event topic0
                            // when configured for this chain.
                            if ( auto it = config.bridge_contract_addresses.find( ep.chain_id );
                                 it != config.bridge_contract_addresses.end() )
                            {
                                wrep.bridge_contract_address = it->second;
                            }
                            if ( auto it = config.bridge_event_topic0.find( ep.chain_id );
                                 it != config.bridge_event_topic0.end() )
                            {
                                wrep.event_topic0 = it->second;
                            }

                            endpoints_by_chain[ep.chain_id].push_back( std::move( wrep ) );
                        }
                    }
                }
            }
        }

        // ── Direct API-key endpoints from the app layer ───────────────
        for ( const auto &[chain_id_str, eps] : config.direct_endpoints )
        {
            uint64_t chain_id = 0;
            try
            {
                chain_id = std::stoull( chain_id_str );
            }
            catch ( ... )
            {
                logger->warn( "ChainRpcEndpointProvider: invalid chain ID '{}' in direct endpoints",
                              chain_id_str );
                continue;
            }

            auto &target = endpoints_by_chain[chain_id];
            for ( const auto &ep : eps )
            {
                WeightedRpcEndpoint wrep{ ep.url, kDirectEndpointWeight, {}, {} };

                // D-02, D-05: carry bridge contract address and event topic0
                // when configured for this chain.
                if ( auto it = config.bridge_contract_addresses.find( chain_id );
                     it != config.bridge_contract_addresses.end() )
                {
                    wrep.bridge_contract_address = it->second;
                }
                if ( auto it = config.bridge_event_topic0.find( chain_id );
                     it != config.bridge_event_topic0.end() )
                {
                    wrep.event_topic0 = it->second;
                }

                target.push_back( std::move( wrep ) );
            }
        }

        // ── Wire endpoints into the validator ─────────────────────────
        bool any_wired = false;
        for ( auto &[chain_id, endpoints] : endpoints_by_chain )
        {
            const auto count = endpoints.size();
            validator.SetRpcEndpoints( std::to_string( chain_id ), std::move( endpoints ) );
            logger->info( "ChainRpcEndpointProvider: wired {} RPC endpoints for chain_id={}",
                          count, chain_id );
            any_wired = true;
        }

        if ( !any_wired )
        {
            logger->warn( "ChainRpcEndpointProvider: no RPC endpoints found for any configured chain" );
        }

        return any_wired;
    }
} // namespace sgns