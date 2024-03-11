/**
 * @file       RouterFactory.hpp
 * @brief      
 * @date       2024-03-04
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _ROUTER_FACTORY_HPP_
#define _ROUTER_FACTORY_HPP_

#include "network/impl/router_libp2p.hpp"
#include <libp2p/injector/host_injector.hpp>
#include "network/production_observer.hpp"
#include "verification/finality/round_observer.hpp"
#include "network/sync_protocol_observer.hpp"
#include "network/extrinsic_observer.hpp"
#include "network/gossiper.hpp"
#include "application/configuration_storage.hpp"
#include "network/types/own_peer_info.hpp"

class CComponentFactory;
namespace sgns
{
    class RouterFactory
    {
    public:
        std::shared_ptr<network::Router> create()
        {
            auto component_factory = SINGLETONINSTANCE( CComponentFactory );

            auto  p2p_injector = libp2p::injector::makeHostInjector<BOOST_DI_CFG>();
            auto &host         = p2p_injector.template create<libp2p::Host &>();

            auto result = component_factory->GetComponent( "ProductionObserver", boost::none );
            if ( !result )
            {
                throw std::runtime_error( "Initialize ProductionObserver first" );
            }
            auto prod_observer = std::dynamic_pointer_cast<network::ProductionObserver>( result.value() );

            result = component_factory->GetComponent( "RoundObserver", boost::none );
            if ( !result )
            {
                throw std::runtime_error( "Initialize RoundObserver first" );
            }
            auto round_observer = std::dynamic_pointer_cast<verification::finality::RoundObserver>( result.value() );

            result = component_factory->GetComponent( "SyncProtocolObserver", boost::none );
            if ( !result )
            {
                throw std::runtime_error( "Initialize SyncProtocolObserver first" );
            }
            auto sync_prot_observer = std::dynamic_pointer_cast<network::SyncProtocolObserver>( result.value() );

            result = component_factory->GetComponent( "ExtrinsicObserver", boost::none );
            if ( !result )
            {
                throw std::runtime_error( "Initialize ExtrinsicObserver first" );
            }
            auto extrinsic_observer = std::dynamic_pointer_cast<network::ExtrinsicObserver>( result.value() );

            result = component_factory->GetComponent( "Gossiper", boost::none );
            if ( !result )
            {
                throw std::runtime_error( "Initialize Gossiper first" );
            }
            auto gossiper = std::dynamic_pointer_cast<network::Gossiper>( result.value() );

            result = component_factory->GetComponent( "ConfigurationStorage", boost::none );
            if ( !result )
            {
                throw std::runtime_error( "Initialize ConfigurationStorage first" );
            }
            auto config_storage = std::dynamic_pointer_cast<application::ConfigurationStorage>( result.value() );

            result = component_factory->GetComponent( "OwnPeerInfo", boost::none );
            if ( !result )
            {
                throw std::runtime_error( "Initialize OwnPeerInfo first" );
            }
            auto own_peer_info = std::dynamic_pointer_cast<network::OwnPeerInfo>( result.value() );

            return std::make_shared<network::RouterLibp2p>( //
                host,                                       //
                prod_observer,                              //
                round_observer,                             //
                sync_prot_observer,                         //
                extrinsic_observer,                         //
                gossiper,                                   //
                config_storage->getBootNodes(),             //
                *own_peer_info                              //
            );
        }
    };

}

#endif