/**
 * @file       Migration1_0_0To3_4_0.cpp
 * @brief      
 * @date       2025-10-03
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "Migration1_0_0To3_4_0.hpp"

namespace sgns
{

    Migration1_0_0To3_4_0::Migration1_0_0To3_4_0( std::shared_ptr<crdt::GlobalDB> db ) : db_( std::move( db ) ) {}

    Migration1_0_0To3_4_0::~Migration1_0_0To3_4_0() {}

    std::string Migration1_0_0To3_4_0::FromVersion() const
    {
        return "1.0.0";
    }

    std::string Migration1_0_0To3_4_0::ToVersion() const
    {
        return "3.4.0";
    }

    outcome::result<bool> Migration1_0_0To3_4_0::IsRequired() const
    {
        sgns::crdt::GlobalDB::Buffer version_key;
        version_key.put( std::string( MigrationManager::VERSION_INFO_KEY ) );
        auto version_ret = db_->GetDataStore()->get( version_key );

        if ( version_ret.has_error() )
        {
            // No version info found, migration is required
            logger_->info( "No version info found in GlobalDB, migration from {} to {} is required",
                           FromVersion(),
                           ToVersion() );
            return true;
        }

        auto version_buffer = version_ret.value();

        if ( !IsVersionLessThan( std::string(version_buffer.toString()), ToVersion() ) )

        {
            logger_->info( "GlobalDB already at target version {}, skipping migration", ToVersion() );
            return false; // Already at target version
        }
        else if ( version_buffer.toString() != FromVersion() )
        {
            logger_->warn( "GlobalDB at unexpected version {}, expected {}, migration may not be applicable",
                           version_buffer.toString(),
                           FromVersion() );
            return false; // Unexpected version, skip migration
        }

        logger_->info( "GlobalDB at version {}, migration to {} is required", FromVersion(), ToVersion() );
        return true; // Migration is required
    }

    outcome::result<void> Migration1_0_0To3_4_0::Apply()
    {
        logger_->info( "Starting migration from {} to {}", FromVersion(), ToVersion() );

        // In version 3.4.0, the full node topic format changed to include the network ID.
        // We need to update any existing topics in the GlobalDB to the new format.

        const std::string old_full_node_topic_prefix = std::string( TransactionManager::GNUS_FULL_NODES_TOPIC_LEGACY );
        const std::string new_full_node_topic_prefix = std::string( TransactionManager::GNUS_FULL_NODES_TOPIC );

        // Query all keys with the old full node topic prefix
        OUTCOME_TRY( auto head_list, db_->GetCRDTHeadList() );

        auto [head_map, maxHeight] = head_list;

        for ( const auto &[topic, cid_set] : head_map )
        {
            if ( topic == old_full_node_topic_prefix )
            {
                logger_->info( "{}: Found old full node topic, replacing it: {}", __func__, topic );
                for ( const auto &cid : cid_set )
                {
                    logger_->info( "{}: Migrating head CID {} from old topic to new topic",
                                   __func__,
                                   cid.toString().value() );
                    OUTCOME_TRY( auto &&head_height, db_->GetCRDTHeadHeight( cid, old_full_node_topic_prefix ) );
                    OUTCOME_TRY( db_->CRDTHeadRemove( cid, old_full_node_topic_prefix ) );
                    OUTCOME_TRY( db_->CRDTHeadAdd( cid, new_full_node_topic_prefix, head_height ) );
                }
            }
        }
        sgns::crdt::GlobalDB::Buffer version_buffer;
        sgns::crdt::GlobalDB::Buffer version_key;
        version_key.put( std::string( MigrationManager::VERSION_INFO_KEY ) );
        version_buffer.put( ToVersion() );

        OUTCOME_TRY( db_->GetDataStore()->put( version_key, version_buffer ) );
        logger_->debug( "Migration from {} to {} completed successfully", FromVersion(), ToVersion() );

        return outcome::success();
    }
}
