/**
 * @file       ProductionSynchronizerFactory.hpp
 * @brief      
 * @date       2024-02-23
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _PRODUCTION_SYNCHRONIZER_FACTORY_HPP_
#define _PRODUCTION_SYNCHRONIZER_FACTORY_HPP_
#include "verification/production/impl/production_synchronizer_impl.hpp"
#include "integration/CComponentFactory.hpp"
#include "application/configuration_storage.hpp"
#include "blockchain/block_tree.hpp"
#include "blockchain/block_header_repository.hpp"
#include "network/types/own_peer_info.hpp"
#include "network/impl/remote_sync_protocol_client.hpp"
#include "network/impl/dummy_sync_protocol_client.hpp"
#include <libp2p/injector/host_injector.hpp>

class ProductionSynchronizerFactory
{

public:
    static std::shared_ptr<sgns::verification::ProductionSynchronizer> create()
    {
        auto component_factory = SINGLETONINSTANCE( CComponentFactory );

        auto result = component_factory->GetComponent( "ConfigurationStorage", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize ConfigurationStorage first" );
        }
        auto config_storage = std::dynamic_pointer_cast<sgns::application::ConfigurationStorage>( result.value() );

        result = component_factory->GetComponent( "BlockTree", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize BlockTree first" );
        }
        auto block_tree = std::dynamic_pointer_cast<sgns::blockchain::BlockTree>( result.value() );

        result = component_factory->GetComponent( "BlockHeaderRepository", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize BlockHeaderRepository first" );
        }
        auto block_header_repo = std::dynamic_pointer_cast<sgns::blockchain::BlockHeaderRepository>( result.value() );

        result = component_factory->GetComponent( "OwnPeerInfo", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize OwnPeerInfo first" );
        }
        auto own_peer_info = std::dynamic_pointer_cast<sgns::network::OwnPeerInfo>( result.value() );

        auto p2p_injector = libp2p::injector::makeHostInjector<BOOST_DI_CFG>();

        auto host = p2p_injector.template create<std::shared_ptr<libp2p::Host>>();

        auto peer_infos = config_storage->getBootNodes().peers;

        auto res = std::make_shared<sgns::network::SyncClientsSet>();

        for ( auto &peer_info : peer_infos )
        {
            spdlog::debug( "Added peer with id: {}", peer_info.id.toBase58() );
            if ( peer_info.id != own_peer_info->id )
            {
                res->clients.emplace_back( std::make_shared<sgns::network::RemoteSyncProtocolClient>( *host, std::move( peer_info ) ) );
            }
            else
            {
                res->clients.emplace_back( std::make_shared<sgns::network::DummySyncProtocolClient>() );
            }
        }
        std::reverse( res->clients.begin(), res->clients.end() );

        return std::make_shared<sgns::verification::ProductionSynchronizerImpl>(res);
    }
};


#endif