/**
 * @file       crdt_callback_manager.hpp
 * @brief      CRDT callback manager header for when an element gets added/removed
 * @date       2025-09-05
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_CRDT_CALLBACK_MANAGER_HPP
#define SGNS_CRDT_CALLBACK_MANAGER_HPP

#include <functional>
#include <memory>
#include <regex>
#include <string>
#include <shared_mutex>
#include <vector>

#include "base/buffer.hpp"
#include "base/logger.hpp"

namespace sgns::crdt
{
    class CRDTWorkJournal;

    class CRDTCallbackManager
    {
    public:
        using NewDataPair     = std::pair<std::string, base::Buffer>;
        using NewDataCallback = std::function<void( NewDataPair new_data, std::string cid )>;

        struct NewDataCallbackEntry
        {
            std::string     pattern;
            std::regex      regex;
            NewDataCallback callback;
        };

        using NewDataCallbackRegistry = std::vector<std::shared_ptr<const NewDataCallbackEntry>>;

        using DeletedDataCallback = std::function<void( std::string deleted_key, std::string cid )>;

        struct DeletedDataCallbackEntry
        {
            std::string         pattern;
            std::regex          regex;
            DeletedDataCallback callback;
        };

        using DeletedDataCallbackRegistry = std::vector<std::shared_ptr<const DeletedDataCallbackEntry>>;

        /**
         * @brief       Construct a new CRDTCallbackManager object
         */
        explicit CRDTCallbackManager( std::shared_ptr<CRDTWorkJournal> work_journal );
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
         * @param[in]   cid content identifier associated with the value
         */
        void PutDataCallback( const std::string &key, const base::Buffer &value, const std::string &cid );

        /**
         * @brief       Executes a registered deleted data callback that matches the key
         * @param[in]   deleted_key key of the CRDT that was deleted
         * @param[in]   cid content identifier associated with the deletion
         */
        void DeleteDataCallback( const std::string &deleted_key, const std::string &cid );

    private:
        std::shared_ptr<CRDTWorkJournal> work_journal_;
        std::shared_mutex new_data_callback_registry_mutex_; ///< Mutex to manipulate @ref new_data_callback_registry_
        NewDataCallbackRegistry new_data_callback_registry_; ///< New data callback registry
        std::shared_mutex
            deleted_data_callback_registry_mutex_; ///< Mutex to manipulate @ref deleted_data_callback_registry_
        DeletedDataCallbackRegistry deleted_data_callback_registry_; ///< Deleted data callback registry

        base::Logger logger_ = base::createLogger( "CRDTCallbackManager" ); ///< Logger instance
    };

}

#endif // SGNS_CRDT_CALLBACK_MANAGER_HPP
