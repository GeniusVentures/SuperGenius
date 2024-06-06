/**
 * @file       TrieStorageBackendFactory.hpp
 * @brief      
 * @date       2024-02-25
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _TRIE_STORAGE_BACKEND_FACTORY_HPP_
#define _TRIE_STORAGE_BACKEND_FACTORY_HPP_
#include "base/buffer.hpp"
#include "blockchain/impl/storage_util.hpp"
#include "singleton/CComponentFactory.hpp"
#include "storage/trie/impl/trie_storage_backend_impl.hpp"

class TrieStorageBackendFactory
{
public:
    static std::shared_ptr<sgns::storage::trie::TrieStorageBackend> create()
    {
        auto component_factory = SINGLETONINSTANCE( CComponentFactory );

        auto maybe_buffer_storage = component_factory->GetComponent( "BufferStorage", boost::make_optional(std::string("rocksdb")) );
        if ( !maybe_buffer_storage )
        {
            throw std::runtime_error( "Initialize BufferStorage first" );
        }
        auto buffer_storage = std::dynamic_pointer_cast<sgns::storage::BufferStorage>( maybe_buffer_storage.value() );

        using sgns::blockchain::prefix::TRIE_NODE;
        return std::make_shared<sgns::storage::trie::TrieStorageBackendImpl>( buffer_storage, sgns::base::Buffer{ TRIE_NODE } );
    }
};

#endif
