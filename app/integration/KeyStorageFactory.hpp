/**
 * @file       KeyStorageFactory.hpp
 * @brief      
 * @date       2024-02-22
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _KEY_STORAGE_FACTORY_HPP_
#define _KEY_STORAGE_FACTORY_HPP_

#include "application/impl/local_key_storage.hpp"

class KeyStorageFactory
{
public:
    static std::shared_ptr<sgns::application::KeyStorage> create( const std::string &path )
    {

        auto result = sgns::application::LocalKeyStorage::create( path );

        if ( result )
        {
            return result.value();
        }
        else
        {
            throw std::runtime_error( "Key storage not found on keystore path" );
        }
    }
};

#endif