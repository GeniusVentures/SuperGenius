/**
 * @file       StorageChangesTrackerFactory.hpp
 * @brief      
 * @date       2024-02-25
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _STORAGE_CHANGES_TRACKER_FACTORY_HPP_
#define _STORAGE_CHANGES_TRACKER_FACTORY_HPP_

#include "storage/changes_trie/impl/storage_changes_tracker_impl.hpp"
#include "subscription/subscriber.hpp"
#include "api/service/api_service.hpp"
#include "base/buffer.hpp"
#include "primitives/common.hpp"

class StorageChangesTrackerFactory
{
public:
    static std::shared_ptr<sgns::storage::changes_trie::ChangesTracker> create()
    {
        auto component_factory    = SINGLETONINSTANCE( CComponentFactory );
        auto maybe_sgns_trie_fact = component_factory->GetComponent( "SuperGeniusTrieFactory", boost::none );
        if ( !maybe_sgns_trie_fact )
        {
            throw std::runtime_error( "Initialize SuperGeniusTrieFactory first" );
        }

        auto sgns_trie_fact = std::dynamic_pointer_cast<sgns::storage::trie::SuperGeniusTrieFactory>( maybe_sgns_trie_fact.value() );

        auto maybe_codec = component_factory->GetComponent( "Codec", boost::none );
        if ( !maybe_codec )
        {
            throw std::runtime_error( "Initialize Codec first" );
        }
        auto codec = std::dynamic_pointer_cast<sgns::storage::trie::Codec>( maybe_codec.value() );

        using SubscriptionEngineType = sgns::subscription::SubscriptionEngine<sgns::base::Buffer, std::shared_ptr<sgns::api::Session>,
                                                                              sgns::base::Buffer, sgns::primitives::BlockHash>;
        auto subscription_engine     = std::make_shared<SubscriptionEngineType>();
        return std::make_shared<sgns::storage::changes_trie::StorageChangesTrackerImpl>( sgns_trie_fact, codec, subscription_engine );
    }
};

#endif