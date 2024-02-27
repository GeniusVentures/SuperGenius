/**
 * @file       ExtrinsicGossiperFactory.hpp
 * @brief      
 * @date       2024-02-27
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _EXTRINSIC_GOSSIPSER_FACTORY_HPP_
#define _EXTRINSIC_GOSSIPSER_FACTORY_HPP_

#include "network/impl/gossiper_broadcast.hpp"
#include <libp2p/injector/host_injector.hpp>
#include <libp2p/log/logger.hpp>

class ExtrinsicGossiperFactory
{
public:
    static std::shared_ptr<sgns::network::ExtrinsicGossiper> create()
    {
        auto p2p_injector     = libp2p::injector::makeHostInjector<BOOST_DI_CFG>();

        auto &host = p2p_injector.template create<libp2p::Host&>();

        return std::make_shared<sgns::network::GossiperBroadcast>(host);
    }
};

#endif
