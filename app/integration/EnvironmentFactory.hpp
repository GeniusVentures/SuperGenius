/**
 * @file       EnvironmentFactory.hpp
 * @brief      
 * @date       2024-03-04
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _ENVIRONMENT_FACTORY_HPP_
#define _ENVIRONMENT_FACTORY_HPP_
#include "verification/finality/impl/environment_impl.hpp"
#include "singleton/CComponentFactory.hpp"
#include "blockchain/block_tree.hpp"
#include "blockchain/block_header_repository.hpp"
#include "verification/finality/gossiper.hpp"

namespace sgns
{

    class EnvironmentFactory
    {
    public:
        std::shared_ptr<verification::finality::Environment> create()
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

            result = component_factory->GetComponent( "Gossiper", boost::none );
            if ( !result )
            {
                throw std::runtime_error( "Initialize Gossiper first" );
            }
            auto gossiper = std::dynamic_pointer_cast<verification::finality::Gossiper>( result.value() );

            return std::make_shared<verification::finality::EnvironmentImpl>( //
                block_tree,                                                   //
                block_header_repo,                                            //
                gossiper                                                      //
            );
        }
    };
}


#endif
