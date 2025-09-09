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
        using NewDataPair      = std::pair<std::string, base::Buffer>;
        using NewDataCallback  = std::function<void( NewDataPair new_data )>;
        using CallbackRegistry = std::unordered_map<std::string, NewDataCallback>;

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
        bool RegisterDataCallback( const std::string &pattern, NewDataCallback callback );

        /**
         * @brief       Removes the registration of an element filter that corresponds to a pattern
         * @param[in]   pattern The regex/pattern that the key of the element has to match
         */
        void UnregisterDataCallback( const std::string &pattern );

        void PutDataCallback( const std::string &key, const base::Buffer &value );

    private:
        std::shared_mutex callback_registry_mutex_;
        CallbackRegistry  callback_registry_;
    };

}
