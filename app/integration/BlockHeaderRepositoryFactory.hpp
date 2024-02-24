
/**
 * @file       BlockHeaderRepositoryFactory.hpp
 * @brief      
 * @date       2024-02-23
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _BLOCK_HEADER_REPOSITORY_FACTORY_HPP_
#define _BLOCK_HEADER_REPOSITORY_FACTORY_HPP_

class BlockHeaderRepositoryFactory
{
public:
    static std::shared_ptr<sgns::blockchain::BlockHeaderRepository> create( const std::string &type )
    {
        auto component_factory = SINGLETONINSTANCE( CComponentFactory );

        auto buf_storage = component_factory->GetComponent( "BufferStorage", type );

        if ( !buf_storage )
        {
            throw std::runtime_error( "Initialize BufferStorage first" );
        }
        auto hasher = component_factory->GetComponent( "Hasher", boost::none );
        if ( !hasher )
        {
            throw std::runtime_error( "Initialize Hasher first" );
        }
        auto result = std::make_shared<sgns::blockchain::KeyValueBlockHeaderRepository>( buf_storage.value(), hasher.value() );
        if ( result )
        {
            return result.value();
        }
        else
        {
            throw std::runtime_error( "BlockHeaderRepository not created" );
        }
    }
}

#endif