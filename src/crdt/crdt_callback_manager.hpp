/**
 * @file       crdt_callback_manager.hpp
 * @brief      
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
         * @brief       Registers an element  callback
         * @param[in]   pattern The regex/pattern that the key of the element has to match
         * @param[in]   filter The callback that is executed in case the pattern matches
         * @return      true if succeeded, false otherwise
         */
        bool RegisterNewDataCallback( const std::string &pattern, NewDataCallback callback );

        /**
         * @brief       Registers an element  callback
         * @param[in]   pattern The regex/pattern that the key of the element has to match
         * @param[in]   filter The callback that is executed in case the pattern matches
         * @return      true if succeeded, false otherwise
         */
        bool RegisterDeletedDataCallback( const std::string &pattern, DeletedDataCallback callback );

        /**
         * @brief       Removes the registration of an element filter that corresponds to a pattern
         * @param[in]   pattern The regex/pattern that the key of the element has to match
         */
        void UnregisterNewDataCallback( const std::string &pattern );
        /**
         * @brief       Removes the registration of an element filter that corresponds to a pattern
         * @param[in]   pattern The regex/pattern that the key of the element has to match
         */
        void UnregisterDeletedDataCallback( const std::string &pattern );

        void PutDataCallback( const std::string &key, const base::Buffer &value );

        void DeleteDataCallback( const std::string &deleted_key );

    private:
        std::shared_mutex       new_data_callback_registry_mutex_;
        NewDataCallbackRegistry new_data_callback_registry_;

        std::shared_mutex           deleted_data_callback_registry_mutex_;
        DeletedDataCallbackRegistry deleted_data_callback_registry_;
    };

}
