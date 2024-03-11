/**
 * @file       SyncProtocolObserverFactory.hpp
 * @brief      
 * @date       2024-03-04
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _SYNC_PROTOCOL_OBSERVER_FACTORY_HPP_
#define _SYNC_PROTOCOL_OBSERVER_FACTORY_HPP_

#include "network/impl/sync_protocol_observer_impl.hpp"

class CComponentFactory;
namespace sgns
{
    class SyncProtocolObserverFactory
    {
    public:
        std::shared_ptr<network::SyncProtocolObserver> create()
        {
            auto component_factory = SINGLETONINSTANCE( CComponentFactory );

            auto result = component_factory->GetComponent( "BlockTree", boost::none );
            if ( !result )
            {
                throw std::runtime_error( "Initialize BlockTree first" );
            }
            auto block_tree = std::dynamic_pointer_cast<blockchain::BlockTree>( result.value() );

            result = component_factory->GetComponent( "BlockHeaderRepository", boost::none );
            if ( !result )
            {
                throw std::runtime_error( "Initialize BlockHeaderRepository first" );
            }
            auto block_header_repo = std::dynamic_pointer_cast<blockchain::BlockHeaderRepository>( result.value() );

            return std::make_shared<network::SyncProtocolObserverImpl>( //
                block_tree,                                             //
                block_header_repo                                       //
            );
        }
    };
}

#endif
