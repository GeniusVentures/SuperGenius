/**
 * @file       crdt_callback_manager.cpp
 * @brief      CRDT callback manager header for when an element gets added/removed
 * @date       2025-09-06
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include <regex>
#include "crdt/crdt_callback_manager.hpp"

namespace sgns::crdt
{
    CRDTCallbackManager::CRDTCallbackManager() {}

    CRDTCallbackManager::~CRDTCallbackManager() {}

    bool CRDTCallbackManager::RegisterNewDataCallback( const std::string &pattern, NewDataCallback callback )
    {
        bool            ret = false;
        std::lock_guard lock( new_data_callback_registry_mutex_ );
        if ( new_data_callback_registry_.find( pattern ) == new_data_callback_registry_.end() )
        {
            new_data_callback_registry_[pattern] = std::move( callback );

            ret = true;
        }
        return ret;
    }

    bool CRDTCallbackManager::RegisterDeletedDataCallback( const std::string &pattern, DeletedDataCallback callback )
    {
        bool            ret = false;
        std::lock_guard lock( deleted_data_callback_registry_mutex_ );
        if ( deleted_data_callback_registry_.find( pattern ) == deleted_data_callback_registry_.end() )
        {
            deleted_data_callback_registry_[pattern] = std::move( callback );

            ret = true;
        }
        return ret;
    }

    void CRDTCallbackManager::UnregisterNewDataCallback( const std::string &pattern )
    {
        std::lock_guard lock( new_data_callback_registry_mutex_ );
        new_data_callback_registry_.erase( pattern );
    }

    void CRDTCallbackManager::UnregisterDeletedDataCallback( const std::string &pattern )
    {
        std::lock_guard lock( deleted_data_callback_registry_mutex_ );
        deleted_data_callback_registry_.erase( pattern );
    }

    void CRDTCallbackManager::PutDataCallback( const std::string  &key,
                                               const base::Buffer &value,
                                               const std::string  &cid )
    {
        NewDataCallbackRegistry registry_copy;
        {
            std::shared_lock lock( new_data_callback_registry_mutex_ );
            registry_copy = new_data_callback_registry_;
        }
        for ( const auto &[pattern, callback] : registry_copy )
        {
            if ( std::regex regex( pattern ); std::regex_match( key, regex ) )
            {
                callback( std::make_pair( key, value ), cid );
            }
        }
    }

    void CRDTCallbackManager::DeleteDataCallback( const std::string &deleted_key, const std::string &cid )
    {
        DeletedDataCallbackRegistry registry_copy;
        {
            std::shared_lock lock( deleted_data_callback_registry_mutex_ );
            registry_copy = deleted_data_callback_registry_;
        }
        for ( const auto &[pattern, callback] : registry_copy )
        {
            if ( std::regex regex( pattern ); std::regex_match( deleted_key, regex ) )
            {
                callback( deleted_key, cid );
            }
        }
    }

}
