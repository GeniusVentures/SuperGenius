/**
 * @file       ProductionFactory.hpp
 * @brief      
 * @date       2024-02-22
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _PRODUCTION_FACTORY_HPP_
#define _PRODUCTION_FACTORY_HPP_

#include "integration/CComponentFactory.hpp"
#include "application/app_state_manager.hpp"

class ProductionFactory
{
public:
    static std::shared_ptr<sgns::verification::Production> create( const std::string &db_path )
    {
        auto component_factory = SINGLETONINSTANCE( CComponentFactory );

        auto maybe_app_st_manager = component_factory->GetComponent( "AppStateManager", boost::none );
        if ( !maybe_app_st_manager )
        {
            throw std::runtime_error( "Initialize AppStateManager first" );
        }
        auto app_st_manager = std::dynamic_pointer_cast<sgns::application::AppStateManager>( maybe_app_st_manager.value() );

        auto maybe_prod_lottery = component_factory->GetComponent( "ProductionLottery", boost::none );
        if ( !maybe_prod_lottery )
        {
            throw std::runtime_error( "Initialize ProductionLottery first" );
        }
        auto prod_lottery = std::dynamic_pointer_cast<sgns::verification::ProductionLottery>( maybe_prod_lottery.value() );

        auto maybe_block_executor = component_factory->GetComponent( "BlockExecutor", boost::none );
        if ( !maybe_block_executor )
        {
            throw std::runtime_error( "Initialize BlockExecutor first" );
        }
        auto block_executor = std::dynamic_pointer_cast<sgns::verification::BlockExecutor>( maybe_block_executor.value() );

        );
        return std::make_shared<verification::ProductionImpl>( //
            app_st_manager,                                    //
            prod_lottery,                                      //
            block_executor,                                    //
        );

        //    BlockExecutor(std::shared_ptr<blockchain::BlockTree> block_tree,
        //                  std::shared_ptr<primitives::ProductionConfiguration> configuration,
        //                  std::shared_ptr<ProductionSynchronizer> production_synchronizer,
        //                  std::shared_ptr<BlockValidator> block_validator,
        //                  std::shared_ptr<EpochStorage> epoch_storage,
        //                  std::shared_ptr<transaction_pool::TransactionPool> tx_pool,
        //                  std::shared_ptr<crypto::Hasher> hasher,
        //                  std::shared_ptr<authority::AuthorityUpdateObserver>
        //                      authority_update_observer);

        /**
    ProductionImpl(std::shared_ptr<application::AppStateManager> app_state_manager,
             std::shared_ptr<ProductionLottery> lottery,
             std::shared_ptr<BlockExecutor> block_executor,
             std::shared_ptr<storage::trie::TrieStorage> trie_db,
             std::shared_ptr<EpochStorage> epoch_storage,
             std::shared_ptr<primitives::ProductionConfiguration> configuration,
             std::shared_ptr<authorship::Proposer> proposer,
             std::shared_ptr<blockchain::BlockTree> block_tree,
             std::shared_ptr<ProductionGossiper> gossiper,
             crypto::SR25519Keypair keypair,
             std::shared_ptr<clock::SystemClock> clock,
             std::shared_ptr<crypto::Hasher> hasher,
             std::unique_ptr<clock::Timer> timer,
             std::shared_ptr<authority::AuthorityUpdateObserver>
                 authority_update_observer);
         */
    }
};

#endif
