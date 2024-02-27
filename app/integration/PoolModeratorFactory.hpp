/**
 * @file       PoolModeratorFactory.hpp
 * @brief      
 * @date       2024-02-27
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _POOL_MODERATOR_FACTORY_HPP_
#define _POOL_MODERATOR_FACTORY_HPP_

#include "transaction_pool/impl/pool_moderator_impl.hpp"
#include "integration/CComponentFactory.hpp"
#include "clock/clock.hpp"

class PoolModeratorFactory
{

public:
    static std::shared_ptr<sgns::transaction_pool::PoolModerator> create()
    {
        auto component_factory = SINGLETONINSTANCE( CComponentFactory );

        auto result = component_factory->GetComponent( "SystemClock", boost::none );

        if ( !result )
        {
            throw std::runtime_error( "Initialize SystemClock first" );
        }
        auto system_clock = std::dynamic_pointer_cast<sgns::clock::SystemClock>( result.value() );

        return std::make_shared<sgns::transaction_pool::PoolModeratorImpl>(system_clock, sgns::transaction_pool::PoolModeratorImpl::Params{});
    }
};


#endif
