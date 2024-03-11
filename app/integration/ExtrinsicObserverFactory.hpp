/**
 * @file       ExtrinsicObserverFactory.hpp
 * @brief      
 * @date       2024-02-26
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _EXTRINSIC_OBSERVER_FACTORY_HPP_
#define _EXTRINSIC_OBSERVER_FACTORY_HPP_
#include "network/impl/extrinsic_observer_impl.hpp"
#include "integration/CComponentFactory.hpp"
#include "api/service/author/author_api.hpp"

class ExtrinsicObserverFactory
{

public:
    static std::shared_ptr<sgns::network::ExtrinsicObserver> create()
    {
        auto component_factory = SINGLETONINSTANCE( CComponentFactory );
        auto result            = component_factory->GetComponent( "AuthorApi", boost::none );

        if ( !result )
        {
            throw std::runtime_error( "Initialize AuthorApi first" );
        }
        auto author_api = std::dynamic_pointer_cast<sgns::api::AuthorApi>( result.value() );

        return std::make_shared<sgns::network::ExtrinsicObserverImpl>( author_api );
    }
};

#endif
