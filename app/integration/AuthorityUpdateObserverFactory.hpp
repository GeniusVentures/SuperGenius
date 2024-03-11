/**
 * @file       AuthorityUpdateObserverFactory.hpp
 * @brief      
 * @date       2024-02-29
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _AUTHORITY_UPDATE_OBSERVER_FACTORY_HPP_
#define _AUTHORITY_UPDATE_OBSERVER_FACTORY_HPP_

#include "verification/authority/impl/authority_manager_impl.hpp"
#include "integration/CComponentFactory.hpp"
#include "application/app_state_manager.hpp"
#include "blockchain/block_tree.hpp"
#include "storage/buffer_map_types.hpp"

class AuthorityUpdateObserverFactory
{
public:
    static std::shared_ptr<sgns::authority::AuthorityUpdateObserver> create()
    {
        auto component_factory = SINGLETONINSTANCE( CComponentFactory );
        auto result            = component_factory->GetComponent( "AppStateManager", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize AppStateManager first" );
        }

            //auto app_state_manager = std::dynamic_pointer_cast<application::AppStateManager>( result.value() );
            auto app_state_manager = AppStateManagerFactory::create();

        result = component_factory->GetComponent( "ProductionConfiguration", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize ProductionConfiguration first" );
        }

        auto prod_config = std::dynamic_pointer_cast<sgns::primitives::ProductionConfiguration>( result.value() );

        result = component_factory->GetComponent( "BlockTree", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize BlockTree first" );
        }

        auto block_tree = std::dynamic_pointer_cast<sgns::blockchain::BlockTree>( result.value() );

        result = component_factory->GetComponent( "BufferStorage", boost::make_optional(std::string("rocksdb")) );
        if ( !result )
        {
            throw std::runtime_error( "Initialize BufferStorage first" );
        }

        auto buff_storage = std::dynamic_pointer_cast<sgns::storage::BufferStorage>( result.value() );

        return std::make_shared<sgns::authority::AuthorityManagerImpl>( //
            app_state_manager,                                          //
            prod_config,                                                //
            block_tree,                                                 //
            buff_storage                                                //
        );
    }
};

#endif
