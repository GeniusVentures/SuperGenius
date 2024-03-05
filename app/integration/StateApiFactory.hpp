/**
 * @file       StateApiFactory.hpp
 * @brief      
 * @date       2024-03-05
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _STATE_API_FACTORY_HPP_
#define _STATE_API_FACTORY_HPP_

#include "api/service/state/impl/state_api_impl.hpp"
#include "blockchain/block_header_repository.hpp"
#include "storage/trie/trie_storage.hpp"
#include "blockchain/block_tree.hpp"

class CComponentFactory;
namespace sgns
{
    class StateApiFactory
    {
        //TODO - Check removed runtime::Core from binaryen
    public:
        std::shared_ptr<api::StateApi> create()
        {
            auto component_factory = SINGLETONINSTANCE( CComponentFactory );
            auto result            = component_factory->GetComponent( "BlockHeaderRepository", boost::none );

            if ( !result )
            {
                throw std::runtime_error( "Initialize BlockHeaderRepository first" );
            }
            auto block_header_repo = std::dynamic_pointer_cast<blockchain::BlockHeaderRepository>( result.value() );

            result = component_factory->GetComponent( "TrieStorage", boost::none );
            if ( !result )
            {
                throw std::runtime_error( "Initialize TrieStorage first" );
            }
            auto trie_storage = std::dynamic_pointer_cast<storage::trie::TrieStorage>( result.value() );

            result = component_factory->GetComponent( "BlockTree", boost::none );
            if ( !result )
            {
                throw std::runtime_error( "Initialize BlockTree first" );
            }
            auto block_tree = std::dynamic_pointer_cast<blockchain::BlockTree>( result.value() );

            return std::make_shared<api::StateApiImpl>( block_header_repo, trie_storage, block_tree );
        }
    };
}

#endif
