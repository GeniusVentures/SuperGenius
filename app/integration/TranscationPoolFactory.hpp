/**
 * @file       TranscationPoolFactory.hpp
 * @brief      
 * @date       2024-02-27
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _TRANSACTION_POOL_FACTORY_HPP_
#define _TRANSACTION_POOL_FACTORY_HPP_

#include "transaction_pool/impl/transaction_pool_impl.hpp"
#include "integration/CComponentFactory.hpp"
#include "transaction_pool/pool_moderator.hpp"
#include "blockchain/block_header_repository.hpp"

class TranscationPoolFactory
{

public:
    static std::shared_ptr<sgns::transaction_pool::TransactionPool> create()
    {
        auto component_factory = SINGLETONINSTANCE( CComponentFactory );

        auto result = component_factory->GetComponent( "PoolModerator", boost::none );

        if ( !result )
        {
            throw std::runtime_error( "Initialize PoolModerator first" );
        }
        auto pool_mod = std::dynamic_pointer_cast<sgns::transaction_pool::PoolModerator>( result.value() );

        result = component_factory->GetComponent( "BlockHeaderRepository", boost::none );

        if ( !result )
        {
            throw std::runtime_error( "Initialize BlockHeaderRepository first" );
        }
        auto block_repo = std::dynamic_pointer_cast<sgns::blockchain::BlockHeaderRepository>( result.value() );
        //TODO - Check this. In the injector this was there don't know if it was being used
        sgns::transaction_pool::TransactionPool::Limits tp_pool_limits{};

        return std::make_shared<sgns::transaction_pool::TransactionPoolImpl>( pool_mod, block_repo, tp_pool_limits );
    }
};

#endif
