/**
 * @file       BlockExecutorFactory.hpp
 * @brief      
 * @date       2024-02-26
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _BLOCK_EXECUTOR_FACTORY_HPP_
#define _BLOCK_EXECUTOR_FACTORY_HPP_

#include "verification/production/impl/block_executor.hpp"
#include "singleton/CComponentFactory.hpp"
#include "blockchain/block_tree.hpp"
#include "primitives/production_configuration.hpp"
#include "verification/production/production_synchronizer.hpp"
#include "verification/validation/block_validator.hpp"
#include "verification/production/epoch_storage.hpp"
#include "transaction_pool/transaction_pool.hpp"
#include "crypto/hasher.hpp"
#include "verification/authority/authority_update_observer.hpp"

class BlockExecutorFactory
{

public:
    static std::shared_ptr<sgns::verification::BlockExecutor> create()
    {
        //TODO - Check the removal of core on BlockExecutor class.
        auto component_factory = SINGLETONINSTANCE( CComponentFactory );

        auto result = component_factory->GetComponent( "BlockTree", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize BlockTree first" );
        }
        auto block_tree = std::dynamic_pointer_cast<sgns::blockchain::BlockTree>( result.value() );

        result = component_factory->GetComponent( "ProductionConfiguration", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize ProductionConfiguration first" );
        }
        auto prod_config = std::dynamic_pointer_cast<sgns::primitives::ProductionConfiguration>( result.value() );

        result = component_factory->GetComponent( "ProductionSynchronizer", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize ProductionSynchronizer first" );
        }
        auto prod_sync = std::dynamic_pointer_cast<sgns::verification::ProductionSynchronizer>( result.value() );

        result = component_factory->GetComponent( "BlockValidator", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize BlockValidator first" );
        }
        auto block_validator = std::dynamic_pointer_cast<sgns::verification::BlockValidator>( result.value() );

        result = component_factory->GetComponent( "EpochStorage", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize EpochStorage first" );
        }
        auto epoch_storage = std::dynamic_pointer_cast<sgns::verification::EpochStorage>( result.value() );

        result = component_factory->GetComponent( "TransactionPool", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize TransactionPool first" );
        }
        auto trans_pool = std::dynamic_pointer_cast<sgns::transaction_pool::TransactionPool>( result.value() );

        result = component_factory->GetComponent( "Hasher", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize Hasher first" );
        }
        auto hasher = std::dynamic_pointer_cast<sgns::crypto::Hasher>( result.value() );

        result = component_factory->GetComponent( "AuthorityUpdateObserver", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize AuthorityUpdateObserver first" );
        }
        auto auth_updt_observer = std::dynamic_pointer_cast<sgns::authority::AuthorityUpdateObserver>( result.value() );

        return std::make_shared<sgns::verification::BlockExecutor>( //
            block_tree,                                             //
            prod_config,                                            //
            prod_sync,                                              //
            block_validator,                                        //
            epoch_storage,                                          //
            trans_pool,                                             //
            hasher,                                                 //
            auth_updt_observer                                      //
        );
    }
};

#endif
