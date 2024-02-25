/**
 * @file       BlockTreeFactory.hpp
 * @brief      
 * @date       2024-02-22
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _BLOCK_TREE_FACTORY_HPP_
#define _BLOCK_TREE_FACTORY_HPP_
#include "integration/CComponentFactory.hpp"
class BlockTreeFactory
{
public:
    static std::shared_ptr<sgns::blockchain::BlockTree> create( const std::string &db_path )
    {
        auto component_factory = SINGLETONINSTANCE( CComponentFactory );
        auto result            = component_factory->GetComponent( "BlockHeaderRepository", boost::none );

        if ( !result )
        {
            throw std::runtime_error( "Initialize BlockHeaderRepository first" );
        }
        auto blk_header = std::dynamic_pointer_cast<sgns::blockchain::BlockHeaderRepository>( result.value() );

        auto result = sgns::blockchain::BlockTreeImpl::create( //
            blk_header.value(),                                //
        );
        else
        {
            throw std::runtime_error( "BlockTree not created" );
        }
    }
}

// outcome::result<std::shared_ptr<BlockTreeImpl>> BlockTreeImpl::create(
//     std::shared_ptr<BlockHeaderRepository> header_repo,
//     std::shared_ptr<BlockStorage> storage,
//     const primitives::BlockId &last_finalized_block,
//     std::shared_ptr<network::ExtrinsicObserver> extrinsic_observer,
//     std::shared_ptr<crypto::Hasher> hasher) {

#endif