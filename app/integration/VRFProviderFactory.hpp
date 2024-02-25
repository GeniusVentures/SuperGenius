/**
 * @file       VRFProviderFactory.hpp
 * @brief      
 * @date       2024-02-22
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _VRF_PROVIDER_FACTORY_HPP_
#define _VRF_PROVIDER_FACTORY_HPP_

#include "crypto/vrf/vrf_provider_impl.hpp"
#include <libp2p/injector/host_injector.hpp>

class VRFProviderFactory
{
public:
    static std::shared_ptr<sgns::crypto::VRFProvider> create()
    {

        //TODO - Remove CSPRNG. Don't like libp2p injector stuff. Probably use crypto3 here

        auto p2p_injector     = libp2p::injector::makeHostInjector<BOOST_DI_CFG>();
        auto random_generator = p2p_injector.template create<std::shared_ptr<sgns::crypto::CSPRNG>>();

        return std::make_shared<sgns::crypto::VRFProviderImpl>( random_generator );
    }
};

#endif
