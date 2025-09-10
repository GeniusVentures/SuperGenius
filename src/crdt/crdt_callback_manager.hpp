/**
 * @file       crdt_callback_manager.hpp
 * @brief      CRDT callback manager header for when an element gets added/removed
 * @date       2025-09-05
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <shared_mutex>

#include "base/buffer.hpp"

namespace sgns::crdt
{

    class CRDTCallbackManager
    {
    public:
        using NewDataPair             = std::pair<std::string, base::Buffer>;
        using NewDataCallback         = std::function<void( NewDataPair new_data )>;
        using NewDataCallbackRegistry = std::unordered_map<std::string, NewDataCallback>;

        using DeletedDataCallback         = std::function<void( std::string deleted_key )>;
        using DeletedDataCallbackRegistry = std::unordered_map<std::string, DeletedDataCallback>;

        /**
         * @brief       Construct a new CRDTCallbackManager object
         */
        explicit CRDTCallbackManager();
        /**
         * @brief      Destroy the CRDTCallbackManager object
         */
        ~CRDTCallbackManager();
        /**
         * @brief       Registers a callback for when new data gets recorded to a specific pattern 
         * @param[in]   pattern Regex pattern that the key should match to call the callback
         * @param[in]   callback The callback itself
         * @return      true if registered, false otherwise
         */
        bool RegisterNewDataCallback( const std::string &pattern, NewDataCallback callback );
        /**
         * @brief       Registers a callback for when data gets deleted to a specific pattern 
         * @param[in]   pattern Regex pattern that the key should match to call the callback
         * @param[in]   callback The callback itself
         * @return      true if registered, false otherwise
         */
        bool RegisterDeletedDataCallback( const std::string &pattern, DeletedDataCallback callback );
        /**
         * @brief       Removes a previously registered new data callback
         * @param[in]   pattern The pattern of the callback to be deleted
         */
        void UnregisterNewDataCallback( const std::string &pattern );
        /**
         * @brief       Removes a previously registered deleted data callback
         * @param[in]   pattern The pattern of the callback to be deleted
         */
        void UnregisterDeletedDataCallback( const std::string &pattern );

        /**
         * @brief       Executes a registered new data callback that matches the key 
         * @param[in]   key key of the CRDT
         * @param[in]   value value contained on the key
         */
        void PutDataCallback( const std::string &key, const base::Buffer &value );

        /**
         * @brief       Executes a registered deleted data callback that matches the key
         * @param[in]   deleted_key key of the CRDT that was deleted
         */
        void DeleteDataCallback( const std::string &deleted_key );

    private:
        std::shared_mutex           new_data_callback_registry_mutex_;     ///< Mutex to manipulate @ref new_data_callback_registry_
        NewDataCallbackRegistry     new_data_callback_registry_;           ///< New data callback registry
        std::shared_mutex           deleted_data_callback_registry_mutex_; ///< Mutex to manipulate @ref deleted_data_callback_registry_
        DeletedDataCallbackRegistry deleted_data_callback_registry_;       ///< Deleted data callback registry
    };

}
