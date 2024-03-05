/**
 * @file       SystemApiFactory.hpp
 * @brief      
 * @date       2024-03-05
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _SYSTEM_API_FACTORY_HPP_
#define _SYSTEM_API_FACTORY_HPP_

#include "api/service/system/impl/system_api_impl.hpp"
#include "blockchain/block_header_repository.hpp"
#include "storage/trie/trie_storage.hpp"
#include "blockchain/block_tree.hpp"

class CComponentFactory;
namespace sgns
{
    class SystemApiFactory
    {
        //TODO - Check removed runtime::Core from binaryen
    public:
        std::shared_ptr<api::SystemApi> create()
        {
            auto component_factory = SINGLETONINSTANCE( CComponentFactory );
            auto result            = component_factory->GetComponent( "ConfigurationStorage", boost::none );

            if ( !result )
            {
                throw std::runtime_error( "Initialize ConfigurationStorage first" );
            }
            auto config_storage = std::dynamic_pointer_cast<application::ConfigurationStorage>( result.value() );

            return std::make_shared<api::SystemApiImpl>( config_storage );
        }
    };
}

#endif