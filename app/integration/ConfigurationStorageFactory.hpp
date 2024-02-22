/**
 * @file       ConfigurationStorageFactory.hpp
 * @brief      
 * @date       2024-02-22
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _CONFIGURATION_STORAGE_FACTORY_HPP_
#define _CONFIGURATION_STORAGE_FACTORY_HPP_

#include "application/impl/configuration_storage_impl.hpp"

class ConfigurationStorageFactory
{
public:
    static std::shared_ptr<sgns::application::ConfigurationStorage> create( const std::string &path )
    {

        auto result = sgns::application::ConfigurationStorageImpl::create( path );

        if ( result )
        {
            return result.value();
        }
        else
        {
            throw std::runtime_error( "Configuration storage not found on Genesis path" );
        }
    }
};

#endif
