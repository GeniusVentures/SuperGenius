/**
 * @file       TrieSerializerFactory.hpp
 * @brief      
 * @date       2024-02-25
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _TRIE_SERIALIZER_FACTORY_HPP_
#define _TRIE_SERIALIZER_FACTORY_HPP_

#include "storage/trie/serialization/trie_serializer_impl.hpp"
#include "integration/CComponentFactory.hpp"

class TrieSerializerFactory
{
public:
    static std::shared_ptr<sgns::storage::trie::TrieSerializer> create()
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

        auto maybe_trie_backend = component_factory->GetComponent( "TrieStorageBackend", boost::none );
        if ( !maybe_trie_backend )
        {
            throw std::runtime_error( "Initialize TrieStorageBackend first" );
        }
        auto trie_backend = std::dynamic_pointer_cast<sgns::storage::trie::TrieStorageBackend>( maybe_trie_backend.value() );

        return std::make_shared<sgns::storage::trie::TrieSerializerImpl>( sgns_trie_fact, codec, trie_backend );
    }
};

#endif
