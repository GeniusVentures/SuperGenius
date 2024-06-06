/**
 * @file       OwnPeerInfoFactory.hpp
 * @brief      
 * @date       2024-02-28
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _OWN_PEER_INFO_FACTORY_HPP_
#define _OWN_PEER_INFO_FACTORY_HPP_

#include "network/types/own_peer_info.hpp"
#include "singleton/CComponentFactory.hpp"
#include "application/key_storage.hpp"
#include <libp2p/injector/host_injector.hpp>
#include "base/outcome_throw.hpp"

class OwnPeerInfoFactory
{
public:
    static std::shared_ptr<sgns::network::OwnPeerInfo> create(const std::uint16_t &port )
    {

        auto component_factory = SINGLETONINSTANCE( CComponentFactory );

        auto result = component_factory->GetComponent( "KeyStorage", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize KeyStorage first" );
        }
        auto key_storage = std::dynamic_pointer_cast<sgns::application::KeyStorage>( result.value() );

        auto                     &&local_pair = key_storage->getP2PKeypair();
        libp2p::crypto::PublicKey &public_key = local_pair.publicKey;

        auto  p2p_injector   = libp2p::injector::makeHostInjector<BOOST_DI_CFG>();
        auto &key_marshaller = p2p_injector.template create<libp2p::crypto::marshaller::KeyMarshaller &>();

        libp2p::peer::PeerId peer_id = libp2p::peer::PeerId::fromPublicKey( key_marshaller.marshal( public_key ).value() ).value();
        spdlog::debug( "Received peer id: {}", peer_id.toBase58() );
        std::string multiaddress_str = "/ip4/0.0.0.0/tcp/" + std::to_string( port );
        spdlog::debug( "Received multiaddr: {}", multiaddress_str );
        auto multiaddress = libp2p::multi::Multiaddress::create( multiaddress_str );
        if ( !multiaddress )
        {
            sgns::base::raise( multiaddress.error() ); // exception
        }
        std::vector<libp2p::multi::Multiaddress> addresses;
        addresses.push_back( std::move( multiaddress.value() ) );

        return std::make_shared<sgns::network::OwnPeerInfo>( std::move( peer_id ), std::move( addresses ) );
    }
};

#endif
