/**
 * @file       BlockTreeFactory.hpp
 * @brief      
 * @date       2024-02-22
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _BLOCK_TREE_FACTORY_HPP_
#define _BLOCK_TREE_FACTORY_HPP_

#include "blockchain/impl/block_tree_impl.hpp"
#include "singleton/CComponentFactory.hpp"
#include "blockchain/block_header_repository.hpp"
#include "blockchain/block_storage.hpp"
#include "network/extrinsic_observer.hpp"
#include "crypto/hasher.hpp"
#include "base/outcome_throw.hpp"
class BlockTreeFactory
{
public:
    static std::shared_ptr<sgns::blockchain::BlockTree> create()
    {
        auto component_factory = SINGLETONINSTANCE( CComponentFactory );
        auto result            = component_factory->GetComponent( "BlockHeaderRepository", boost::none );

        if ( !result )
        {
            throw std::runtime_error( "Initialize BlockHeaderRepository first" );
        }
        auto blk_header = std::dynamic_pointer_cast<sgns::blockchain::BlockHeaderRepository>( result.value() );

        result = component_factory->GetComponent( "BlockStorage", boost::none );

        if ( !result )
        {
            throw std::runtime_error( "Initialize BlockStorage first" );
        }
        auto blk_storage = std::dynamic_pointer_cast<sgns::blockchain::BlockStorage>( result.value() );

        result = component_factory->GetComponent( "ExtrinsicObserver", boost::none );

        if ( !result )
        {
            throw std::runtime_error( "Initialize ExtrinsicObserver first" );
        }
        auto ext_observer = std::dynamic_pointer_cast<sgns::network::ExtrinsicObserver>( result.value() );

        result = component_factory->GetComponent( "Hasher", boost::none );

        if ( !result )
        {
            throw std::runtime_error( "Initialize Hasher first" );
        }
        auto hasher = std::dynamic_pointer_cast<sgns::crypto::Hasher>( result.value() );

        auto last_finalized_block_res = blk_storage->getLastFinalizedBlockHash();
        auto block_id =
            last_finalized_block_res.has_value() ? sgns::primitives::BlockId{ last_finalized_block_res.value() } : sgns::primitives::BlockId{ 0 };

        auto maybe_block_tree = sgns::blockchain::BlockTreeImpl::create( //
            //std::move{blk_header},                   //TODO - It was this, but move might be wrong
            blk_header,  //
            blk_storage, //
            block_id,    //
            //std::move( extrinsic_observer ), //TODO - It was this, but move might be wrong
            //std::move( hasher )              //TODO - It was this, but move might be wrong
            ext_observer, //
            hasher        //

        );

        if ( !maybe_block_tree )
        {
            sgns::base::raise( maybe_block_tree.error() );
        }

        return maybe_block_tree.value();
    }
};


#endif