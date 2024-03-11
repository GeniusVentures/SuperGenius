/**
 * @file       ChainApiFactory.hpp
 * @brief      
 * @date       2024-03-05
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _CHAIN_API_FACTORY_HPP_
#define _CHAIN_API_FACTORY_HPP_

#include "api/service/chain/impl/chain_api_impl.hpp"
#include "blockchain/block_header_repository.hpp"
#include "blockchain/block_tree.hpp"

class CComponentFactory;
namespace sgns
{
    class ChainApiFactory
    {
    public:
        std::shared_ptr<api::ChainApi> create()
        {
            auto component_factory = SINGLETONINSTANCE( CComponentFactory );
            auto result            = component_factory->GetComponent( "BlockHeaderRepository", boost::none );

            if ( !result )
            {
                throw std::runtime_error( "Initialize BlockHeaderRepository first" );
            }
            auto block_header_repo = std::dynamic_pointer_cast<blockchain::BlockHeaderRepository>( result.value() );
            
            result            = component_factory->GetComponent( "BlockTree", boost::none );
            if ( !result )
            {
                throw std::runtime_error( "Initialize BlockTree first" );
            }
            auto block_tree = std::dynamic_pointer_cast<blockchain::BlockTree>( result.value() );

            return std::make_shared<api::ChainApiImpl>(block_header_repo, block_tree);
        }
    };
}

#endif