/**
 * @file       EpochStorageFactory.hpp
 * @brief      
 * @date       2024-02-29
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _EPOCH_STORAGE_HPP_
#define _EPOCH_STORAGE_HPP_
#include "verification/production/impl/epoch_storage_impl.hpp"
#include "integration/CComponentFactory.hpp"
#include "storage/buffer_map_types.hpp"

class EpochStorageFactory
{
public:
    static std::shared_ptr<sgns::verification::EpochStorage> create()
    {
        auto component_factory = SINGLETONINSTANCE( CComponentFactory );

        auto result = component_factory->GetComponent( "BufferStorage", boost::make_optional(std::string("rocksdb")));

        if ( !result )
        {
            throw std::runtime_error( "Initialize BufferStorage first" );
        }
        auto buffer_storage = std::dynamic_pointer_cast<sgns::storage::BufferStorage>( result.value() );

        return std::make_shared<sgns::verification::EpochStorageImpl>( buffer_storage );
    }
};

#endif