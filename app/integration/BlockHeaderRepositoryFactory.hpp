
/**
 * @file       BlockHeaderRepositoryFactory.hpp
 * @brief      
 * @date       2024-02-23
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _BLOCK_HEADER_REPOSITORY_FACTORY_HPP_
#define _BLOCK_HEADER_REPOSITORY_FACTORY_HPP_

#include "blockchain/impl/key_value_block_header_repository.hpp"
#include "integration/CComponentFactory.hpp"

class BlockHeaderRepositoryFactory
{
public:
    static std::shared_ptr<sgns::blockchain::BlockHeaderRepository> create( const std::string &type )
    {
        auto component_factory = SINGLETONINSTANCE( CComponentFactory );

        auto retval = component_factory->GetComponent( "BufferStorage", type );

        if ( !retval )
        {
            throw std::runtime_error( "Initialize BufferStorage first" );
        }
        auto buf_storage = std::dynamic_pointer_cast<sgns::storage::BufferStorage>( retval.value() );
        retval = component_factory->GetComponent( "Hasher", boost::none );
        if ( !retval )
        {
            throw std::runtime_error( "Initialize Hasher first" );
        }
        auto hasher = std::dynamic_pointer_cast<sgns::crypto::Hasher>( retval.value() );

        return std::make_shared<sgns::blockchain::KeyValueBlockHeaderRepository>( buf_storage, hasher );
    }
};

#endif