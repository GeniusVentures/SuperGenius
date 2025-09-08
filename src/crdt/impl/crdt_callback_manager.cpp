/**
 * @file       crdt_callback_manager.cpp
 * @brief      
 * @date       2025-09-06
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include <regex>
#include "crdt/crdt_callback_manager.hpp"

namespace sgns::crdt
{
    CRDTCallbackManager::CRDTCallbackManager() {}

    CRDTCallbackManager::~CRDTCallbackManager() {}

    bool CRDTCallbackManager::RegisterDataCallback( const std::string &pattern, NewDataCallback callback )
    {
        std::lock_guard lock( callback_registry_mutex_ );
        callback_registry_[pattern] = std::move( callback );
    }

    void CRDTCallbackManager::UnregisterDataCallback( const std::string &pattern )
    {
        //
        std::lock_guard lock( callback_registry_mutex_ );
        callback_registry_.erase( pattern );
    }

    void CRDTCallbackManager::DataCallback( const std::string &key, const base::Buffer &value )
    {

        std::unordered_map<std::string, NewDataCallback> registry_copy;
        {
            std::shared_lock lock( callback_registry_mutex_ );
            registry_copy = callback_registry_;
        }
        for ( const auto &[pattern, callback] : registry_copy )
        {
            if ( std::regex regex( pattern ); std::regex_match( key, regex ) )
            {
                callback( key, value );
            }
        }
    }

}
