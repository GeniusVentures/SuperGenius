/**
 * @file       BlockBuilderManager.hpp
 * @brief      
 * @date       2024-03-01
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _BLOCK_BUILDER_MANAGER_HPP
#define _BLOCK_BUILDER_MANAGER_HPP

#include "authorship/impl/block_builder_factory_impl.hpp"
#include "authorship/impl/block_builder_impl.hpp"
#include "integration/CComponentFactory.hpp"
#include "blockchain/block_header_repository.hpp"

namespace sgns
{
    //TODO - Removed runtime::Core and runtime::BlockBuilder due to binaryen
    class BlockBuilderManager
    {
    public:
        std::shared_ptr<authorship::BlockBuilderFactory> createFactory()
        {
            auto component_factory = SINGLETONINSTANCE( CComponentFactory );
            auto result            = component_factory->GetComponent( "BlockHeaderRepository", boost::none );

            if ( !result )
            {
                throw std::runtime_error( "Initialize BlockHeaderRepository first" );
            }
            auto block_header_repo = std::dynamic_pointer_cast<blockchain::BlockHeaderRepository>( result.value() );
            return std::make_shared<authorship::BlockBuilderFactoryImpl>( block_header_repo );
        }
    };
}

#endif
