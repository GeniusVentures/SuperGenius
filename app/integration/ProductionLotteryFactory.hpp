/**
 * @file       ProductionLotteryFactory.hpp
 * @brief      
 * @date       2024-02-22
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _PRODUCTION_LOTTERY_FACTORY_HPP_
#define _PRODUCTION_LOTTERY_FACTORY_HPP_

#include "verification/production/impl/production_lottery_impl.hpp"
#include "crypto/vrf_provider.hpp"
#include "singleton/CComponentFactory.hpp"

class ProductionLotteryFactory
{
public:
    static std::shared_ptr<sgns::verification::ProductionLottery> create()
    {
        auto component_factory = SINGLETONINSTANCE( CComponentFactory );

        auto maybe_vrf_provider = component_factory->GetComponent( "VRFProvider", boost::none );

        if ( !maybe_vrf_provider )
        {
            throw std::runtime_error( "Initialize VRFProvider first" );
        }
        auto vrf_provider = std::dynamic_pointer_cast<sgns::crypto::VRFProvider>( maybe_vrf_provider.value() );
        auto maybe_hasher = component_factory->GetComponent( "Hasher", boost::none );
        if ( !maybe_hasher )
        {
            throw std::runtime_error( "Initialize Hasher first" );
        }
        auto hasher = std::dynamic_pointer_cast<sgns::crypto::Hasher>( maybe_hasher.value() );
        return std::make_shared<sgns::verification::ProductionLotteryImpl>( //
            vrf_provider,                                                   //
            hasher );
    }
};

#endif
