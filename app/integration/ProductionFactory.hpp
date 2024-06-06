/**
 * @file       ProductionFactory.hpp
 * @brief      
 * @date       2024-02-22
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _PRODUCTION_FACTORY_HPP_
#define _PRODUCTION_FACTORY_HPP_

#include "singleton/CComponentFactory.hpp"
#include "verification/production/impl/production_impl.hpp"
#include "application/app_state_manager.hpp"
#include "verification/production/production_lottery.hpp"
#include "verification/production/impl/block_executor.hpp"
#include "storage/trie/trie_storage.hpp"
#include "verification/production/epoch_storage.hpp"
#include "primitives/production_configuration.hpp"
#include "authorship/proposer.hpp"
#include "blockchain/block_tree.hpp"
#include "verification/production/production_gossiper.hpp"
#include "crypto/sr25519_types.hpp"
#include "clock/clock.hpp"
#include "crypto/hasher.hpp"
#include "clock/impl/basic_waitable_timer.hpp"

class ProductionFactory
{
public:
    //TODO - Remove this io_context from the parameter needed for the timer.
    static std::shared_ptr<sgns::verification::Production> create( boost::asio::io_context &io_context )
    {
        auto component_factory = SINGLETONINSTANCE( CComponentFactory );

        auto result = component_factory->GetComponent( "AppStateManager", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize AppStateManager first" );
        }
        //auto app_state_manager = std::dynamic_pointer_cast<application::AppStateManager>( result.value() );
        auto app_state_manager = AppStateManagerFactory::create();

        result = component_factory->GetComponent( "ProductionLottery", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize ProductionLottery first" );
        }
        auto prod_lottery = std::dynamic_pointer_cast<sgns::verification::ProductionLottery>( result.value() );

        result = component_factory->GetComponent( "BlockExecutor", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize BlockExecutor first" );
        }
        auto block_executor = std::dynamic_pointer_cast<sgns::verification::BlockExecutor>( result.value() );

        result = component_factory->GetComponent( "TrieStorage", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize TrieStorage first" );
        }
        auto trie_storage = std::dynamic_pointer_cast<sgns::storage::trie::TrieStorage>( result.value() );

        result = component_factory->GetComponent( "EpochStorage", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize EpochStorage first" );
        }
        auto epoch_storage = std::dynamic_pointer_cast<sgns::verification::EpochStorage>( result.value() );

        result = component_factory->GetComponent( "ProductionConfiguration", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize ProductionConfiguration first" );
        }
        auto prod_config = std::dynamic_pointer_cast<sgns::primitives::ProductionConfiguration>( result.value() );

        result = component_factory->GetComponent( "Proposer", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize Proposer first" );
        }
        auto proposer = std::dynamic_pointer_cast<sgns::authorship::Proposer>( result.value() );

        result = component_factory->GetComponent( "BlockTree", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize BlockTree first" );
        }
        auto block_tree = std::dynamic_pointer_cast<sgns::blockchain::BlockTree>( result.value() );

        result = component_factory->GetComponent( "ProductionGossiper", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize ProductionGossiper first" );
        }
        auto prod_gossiper = std::dynamic_pointer_cast<sgns::verification::ProductionGossiper>( result.value() );

        result = component_factory->GetComponent( "SR25519Keypair", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize SR25519Keypair first" );
        }
        auto sr25519_keypair = std::dynamic_pointer_cast<sgns::crypto::SR25519Keypair>( result.value() );

        result = component_factory->GetComponent( "SystemClock", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize SystemClock first" );
        }
        auto sys_clock = std::dynamic_pointer_cast<sgns::clock::SystemClock>( result.value() );

        result = component_factory->GetComponent( "Hasher", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize Hasher first" );
        }
        auto hasher = std::dynamic_pointer_cast<sgns::crypto::Hasher>( result.value() );

        /**result = component_factory->GetComponent( "Timer", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize Timer first" );
        }
        auto timer = std::dynamic_pointer_cast<sgns::clock::Timer>( result.value() );
        */

        result = component_factory->GetComponent( "AuthorityUpdateObserver", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize AuthorityUpdateObserver first" );
        }
        auto auth_updt_observer = std::dynamic_pointer_cast<sgns::authority::AuthorityUpdateObserver>( result.value() );

        return std::make_shared<sgns::verification::ProductionImpl>(         //
            app_state_manager,                                               //
            prod_lottery,                                                    //
            block_executor,                                                  //
            trie_storage,                                                    //
            epoch_storage,                                                   //
            prod_config,                                                     //
            proposer,                                                        //
            block_tree,                                                      //
            prod_gossiper,                                                   //
            *sr25519_keypair,                                                //
            sys_clock,                                                       //
            hasher,                                                          //
            std::make_unique<sgns::clock::BasicWaitableTimer>( io_context ), // TODO - Use Timer from GetComponent here. unique_ptr from shared_ptr?
            auth_updt_observer                                               //
        );
    }
};

#endif
