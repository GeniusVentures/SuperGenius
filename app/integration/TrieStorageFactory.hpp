/**
 * @file       TrieStorageFactory.hpp
 * @brief      
 * @date       2024-02-25
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _TRIE_STORAGE_HPP_
#define _TRIE_STORAGE_HPP_

#include "storage/trie/impl/trie_storage_impl.hpp"
#include "storage/trie/supergenius_trie/supergenius_trie_factory.hpp"
#include "storage/trie/codec.hpp"
#include "storage/trie/serialization/trie_serializer.hpp"
#include "storage/changes_trie/changes_tracker.hpp"
#include "application/configuration_storage.hpp"
#include "singleton/CComponentFactory.hpp"
#include "base/outcome_throw.hpp"

class TrieStorageFactory
{
public:
    static std::shared_ptr<sgns::storage::trie::TrieStorage> create()
    {
        auto component_factory = SINGLETONINSTANCE( CComponentFactory );

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
        //TODO - Check if works...this is new. In injector layer trie serializer didn't have arguments
        auto maybe_trie_serializer = component_factory->GetComponent( "TrieSerializer", boost::none );
        if ( !maybe_trie_serializer )
        {
            throw std::runtime_error( "Initialize TrieSerializer first" );
        }
        auto trie_serializer = std::dynamic_pointer_cast<sgns::storage::trie::TrieSerializer>( maybe_trie_serializer.value() );

        auto maybe_tracker = component_factory->GetComponent( "ChangesTracker", boost::none );
        if ( !maybe_tracker )
        {
            throw std::runtime_error( "Initialize ChangesTracker first" );
        }
        auto tracker = std::dynamic_pointer_cast<sgns::storage::changes_trie::ChangesTracker>( maybe_tracker.value() );

        auto maybe_trie_storage = sgns::storage::trie::TrieStorageImpl::createEmpty( sgns_trie_fact, codec, trie_serializer, tracker );
        if ( !maybe_trie_storage )
        {
            throw std::runtime_error( "Failed to create TrieStorage" );
        }
        std::shared_ptr<sgns::storage::trie::TrieStorageImpl> trie_storage = std::move( maybe_trie_storage.value() );

        auto maybe_config_storage = component_factory->GetComponent( "ConfigurationStorage", boost::none );
        if ( !maybe_config_storage )
        {
            throw std::runtime_error( "Initialize ConfigurationStorage first" );
        }
        auto config_storage = std::dynamic_pointer_cast<sgns::application::ConfigurationStorage>( maybe_config_storage.value() );

        const auto &genesis_raw_configs = config_storage->getGenesis();
        auto        batch               = trie_storage->getPersistentBatch();
        if ( !batch )
        {
            sgns::base::raise( batch.error() );
        }
        for ( const auto &[key, val] : genesis_raw_configs )
        {
            spdlog::info( "Key: {}, Val: {}", key.toHex(), val.toHex().substr( 0, 200 ) );
            if ( auto res = batch.value()->put( key, val ); !res )
            {
                sgns::base::raise( res.error() );
            }
        }
        if ( auto res = batch.value()->commit(); !res )
        {
            sgns::base::raise( res.error() );
        }

        return trie_storage;
    }
};

#endif