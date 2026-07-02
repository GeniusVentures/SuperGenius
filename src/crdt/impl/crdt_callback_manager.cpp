/**
 * @file       crdt_callback_manager.cpp
 * @brief      CRDT callback manager header for when an element gets added/removed
 * @date       2025-09-06
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include <algorithm>
#include <regex>
#include "crdt/crdt_callback_manager.hpp"
#include "crdt/globaldb/crdt_work_journal.hpp"

namespace sgns::crdt
{
    CRDTCallbackManager::CRDTCallbackManager( std::shared_ptr<CRDTWorkJournal> work_journal ) :
        work_journal_( std::move( work_journal ) )
    {
        logger_->debug( "CRDTCallbackManager constructed" );
    }

    CRDTCallbackManager::~CRDTCallbackManager()
    {
        logger_->debug( "CRDTCallbackManager destroyed" );
    }

    bool CRDTCallbackManager::RegisterNewDataCallback( const std::string &pattern, NewDataCallback callback )
    {
        auto entry = std::shared_ptr<const NewDataCallbackEntry>{};
        try
        {
            entry = std::make_shared<NewDataCallbackEntry>(
                NewDataCallbackEntry{ pattern, std::regex( pattern ), std::move( callback ) } );
        }
        catch ( const std::regex_error &e )
        {
            logger_->error( "Regex error for pattern '{}': {}", pattern, e.what() );
            return false;
        }

        bool            ret = false;
        std::lock_guard lock( new_data_callback_registry_mutex_ );

        logger_->debug( "Attempting to register new data callback for pattern: '{}'", pattern );

        auto it = std::find_if( new_data_callback_registry_.begin(),
                                new_data_callback_registry_.end(),
                                [&]( const auto &registered ) { return registered->pattern == pattern; } );
        if ( it == new_data_callback_registry_.end() )
        {
            new_data_callback_registry_.push_back( std::move( entry ) );
            ret = true;
            logger_->info( "Successfully registered new data callback for pattern: '{}'", pattern );
        }
        else
        {
            logger_->warn( "Pattern '{}' already exists in new data callback registry", pattern );
        }

        logger_->debug( "Total registered new data callbacks: {}", new_data_callback_registry_.size() );
        return ret;
    }

    bool CRDTCallbackManager::RegisterDeletedDataCallback( const std::string &pattern, DeletedDataCallback callback )
    {
        auto entry = std::shared_ptr<const DeletedDataCallbackEntry>{};
        try
        {
            entry = std::make_shared<DeletedDataCallbackEntry>(
                DeletedDataCallbackEntry{ pattern, std::regex( pattern ), std::move( callback ) } );
        }
        catch ( const std::regex_error &e )
        {
            logger_->error( "Regex error for delete pattern '{}': {}", pattern, e.what() );
            return false;
        }

        bool            ret = false;
        std::lock_guard lock( deleted_data_callback_registry_mutex_ );

        logger_->debug( "Attempting to register deleted data callback for pattern: '{}'", pattern );

        auto it = std::find_if( deleted_data_callback_registry_.begin(),
                                deleted_data_callback_registry_.end(),
                                [&]( const auto &registered ) { return registered->pattern == pattern; } );
        if ( it == deleted_data_callback_registry_.end() )
        {
            deleted_data_callback_registry_.push_back( std::move( entry ) );
            ret = true;
            logger_->info( "Successfully registered deleted data callback for pattern: '{}'", pattern );
        }
        else
        {
            logger_->warn( "Pattern '{}' already exists in deleted data callback registry", pattern );
        }

        logger_->debug( "Total registered deleted data callbacks: {}", deleted_data_callback_registry_.size() );
        return ret;
    }

    void CRDTCallbackManager::UnregisterNewDataCallback( const std::string &pattern )
    {
        std::lock_guard lock( new_data_callback_registry_mutex_ );

        auto it = std::find_if( new_data_callback_registry_.begin(),
                                new_data_callback_registry_.end(),
                                [&]( const auto &registered ) { return registered->pattern == pattern; } );
        if ( it != new_data_callback_registry_.end() )
        {
            new_data_callback_registry_.erase( it );
            logger_->info( "Successfully unregistered new data callback for pattern: '{}'", pattern );
        }
        else
        {
            logger_->warn( "Attempted to unregister non-existent pattern: '{}'", pattern );
        }

        logger_->debug( "Total registered new data callbacks after unregister: {}",
                        new_data_callback_registry_.size() );
    }

    void CRDTCallbackManager::UnregisterDeletedDataCallback( const std::string &pattern )
    {
        std::lock_guard lock( deleted_data_callback_registry_mutex_ );

        auto it = std::find_if( deleted_data_callback_registry_.begin(),
                                deleted_data_callback_registry_.end(),
                                [&]( const auto &registered ) { return registered->pattern == pattern; } );
        if ( it != deleted_data_callback_registry_.end() )
        {
            deleted_data_callback_registry_.erase( it );
            logger_->info( "Successfully unregistered deleted data callback for pattern: '{}'", pattern );
        }
        else
        {
            logger_->warn( "Attempted to unregister non-existent pattern: '{}'", pattern );
        }

        logger_->debug( "Total registered deleted data callbacks after unregister: {}",
                        deleted_data_callback_registry_.size() );
    }

    void CRDTCallbackManager::PutDataCallback( const std::string  &key,
                                               const base::Buffer &value,
                                               const std::string  &cid )
    {
        logger_->debug( "PutDataCallback triggered for key: '{}', cid: '{}', value size: {} bytes",
                        key,
                        cid,
                        value.size() );

        NewDataCallbackRegistry registry_snapshot;
        {
            std::shared_lock lock( new_data_callback_registry_mutex_ );
            registry_snapshot = new_data_callback_registry_;
            logger_->debug( "Snapshotted {} registered patterns for matching", registry_snapshot.size() );
        }
        work_journal_->MarkProcessing( key );

        if ( registry_snapshot.empty() )
        {
            logger_->warn( "No new data callbacks registered - key '{}' will not trigger any callbacks", key );
            return;
        }

        bool callback_triggered = false;
        for ( const auto &entry : registry_snapshot )
        {
            logger_->debug( "Testing key '{}' against pattern '{}'", key, entry->pattern );

            const bool matches = std::regex_match( key, entry->regex );
            logger_->debug( "Regex match result for key '{}' vs pattern '{}': {}",
                            key,
                            entry->pattern,
                            matches ? "MATCH" : "NO MATCH" );
            if ( matches )
            {
                logger_->info( "Executing callback for key '{}' matching pattern '{}'", key, entry->pattern );
                entry->callback( std::make_pair( key, value ), cid );
                if ( auto work_entry = work_journal_->GetEntry( key );
                     work_entry.has_value() && work_entry->state == CRDTWorkJournal::State::Processing )
                {
                    if ( !work_journal_->MarkDone( key ) )
                    {
                        logger_->error( "Failed to auto-complete CRDT work for key '{}'", key );
                    }
                }
                callback_triggered = true;
            }
        }

        if ( !callback_triggered )
        {
            logger_->warn( "No callbacks were triggered for key '{}' - no pattern matches found", key );
        }
        else
        {
            logger_->debug( "Successfully triggered callbacks for key '{}'", key );
        }
    }

    void CRDTCallbackManager::DeleteDataCallback( const std::string &deleted_key, const std::string &cid )
    {
        logger_->debug( "DeleteDataCallback triggered for key: '{}', cid: '{}'", deleted_key, cid );

        DeletedDataCallbackRegistry registry_snapshot;
        {
            std::shared_lock lock( deleted_data_callback_registry_mutex_ );
            registry_snapshot = deleted_data_callback_registry_;
            logger_->debug( "Snapshotted {} registered delete patterns for matching", registry_snapshot.size() );
        }

        if ( registry_snapshot.empty() )
        {
            logger_->warn( "No deleted data callbacks registered - key '{}' will not trigger any callbacks",
                           deleted_key );
            return;
        }

        bool callback_triggered = false;
        for ( const auto &entry : registry_snapshot )
        {
            logger_->debug( "Testing deleted key '{}' against pattern '{}'", deleted_key, entry->pattern );

            const bool matches = std::regex_match( deleted_key, entry->regex );
            logger_->debug( "Regex match result for deleted key '{}' vs pattern '{}': {}",
                            deleted_key,
                            entry->pattern,
                            matches ? "MATCH" : "NO MATCH" );
            if ( matches )
            {
                logger_->info( "Executing delete callback for key '{}' matching pattern '{}'",
                               deleted_key,
                               entry->pattern );
                entry->callback( deleted_key, cid );
                callback_triggered = true;
            }
        }

        if ( !callback_triggered )
        {
            logger_->warn( "No delete callbacks were triggered for key '{}' - no pattern matches found", deleted_key );
        }
        else
        {
            logger_->debug( "Successfully triggered delete callbacks for key '{}'", deleted_key );
        }
    }

}
