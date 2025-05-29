/**
 * @file MigrationManager.cpp
 * @brief Implementation of MigrationManager and Migration0To1_0_0.
 * @date 2025-05-29
 * @author Luiz (...)
 */
#include "account/MigrationManager.hpp"

#include <boost/format.hpp>
#include <stdexcept>
#include <utility>

namespace sgns
{
    void MigrationManager::RegisterStep( std::unique_ptr<IMigrationStep> step )
    {
        steps_.push_back( std::move( step ) );
        m_logger->debug( "Registered migration step from {} to {}",
                         steps_.back()->FromVersion(),
                         steps_.back()->ToVersion() );
    }

    outcome::result<void> MigrationManager::Migrate( const std::string &currentVersion,
                                                     const std::string &targetVersion )
    {
        std::string version = currentVersion;
        while ( version != targetVersion )
        {
            bool applied = false;
            for ( auto &step : steps_ )
            {
                if ( step->FromVersion() == version )
                {
                    m_logger->debug( "Applying migration step from {} to {}", version, step->ToVersion() );
                    OUTCOME_TRY( step->Apply() );
                    version = step->ToVersion();
                    applied = true;
                    break;
                }
            }
            if ( !applied )
            {
                m_logger->error( "No migration step found from version {}", version );
                return outcome::failure( boost::system::error_code{} );
            }
        }
        m_logger->debug( "Migration completed successfully from {} to {}", currentVersion, targetVersion );
        return outcome::success();
    }

    Migration0To1_0_0::Migration0To1_0_0(
        std::shared_ptr<crdt::GlobalDB>                                       newDb,
        std::shared_ptr<boost::asio::io_context>                              ioContext,
        std::shared_ptr<ipfs_pubsub::GossipPubSub>                            pubSub,
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Network>            graphsync,
        std::shared_ptr<libp2p::protocol::Scheduler>                          scheduler,
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
        uint16_t                                                              basePort,
        const std::string                                                    &basePath ) :
        newDb_( std::move( newDb ) ),
        ioContext_( std::move( ioContext ) ),
        pubSub_( std::move( pubSub ) ),
        graphsync_( std::move( graphsync ) ),
        scheduler_( std::move( scheduler ) ),
        generator_( std::move( generator ) ),
        basePort_( basePort ),
        basePath_( basePath )
    {
    }

    std::string Migration0To1_0_0::FromVersion() const
    {
        return "0";
    }

    std::string Migration0To1_0_0::ToVersion() const
    {
        return "1.0.0";
    }

    outcome::result<std::shared_ptr<crdt::GlobalDB>> Migration0To1_0_0::InitLegacyDb( const std::string &basePath,
                                                                                      uint16_t           port )
    {
        std::string fullPath = ( boost::format( "%s_%d" ) % basePath % port ).str();
        // m_logger->debug( "Initializing legacy DB at path {}", fullPath );

        auto dbResult = crdt::GlobalDB::New( ioContext_,
                                             fullPath,
                                             pubSub_,
                                             crdt::CrdtOptions::DefaultOptions(),
                                             graphsync_,
                                             scheduler_,
                                             generator_ );

        if ( dbResult.has_error() )
        {
            // m_logger->error( "Failed to create GlobalDB at path {}", fullPath );
            return outcome::failure( boost::system::error_code{} );
        }

        dbResult.value()->Start();
        // m_logger->debug( "Started legacy DB at path {}", fullPath );

        return dbResult.value();
    }

    outcome::result<void> Migration0To1_0_0::MigrateDb( const std::shared_ptr<crdt::GlobalDB> &oldDb,
                                                        const std::shared_ptr<crdt::GlobalDB> &newDb )
    {
        // m_logger->debug( "Starting migration of key-value entries" );
        auto maybeEntries = oldDb->QueryKeyValues( "" );
        if ( !maybeEntries.has_value() )
        {
            // m_logger->error( "Failed to query key-values from old DB" );
            return outcome::failure( boost::system::error_code{} );
        }

        auto &entries = maybeEntries.value();
        auto  tx      = newDb->BeginTransaction();

        for ( auto &entry : entries )
        {
            auto keyOpt = oldDb->KeyToString( entry.first );
            if ( !keyOpt.has_value() )
            {
                // m_logger->error( "Failed to convert key to string during migration" );
                continue;
            }
            crdt::HierarchicalKey hk( keyOpt.value() );
            OUTCOME_TRY( tx->Put( std::move( hk ), std::move( entry.second ) ) );
        }

        if ( tx->Commit().has_error() )
        {
            // m_logger->error( "Failed to commit migrated transaction" );
            return outcome::failure( boost::system::error_code{} );
        }

        // m_logger->debug( "Migration of {} key-values completed", entries.size() );
        return outcome::success();
    }

    outcome::result<void> Migration0To1_0_0::Apply()
    {
        // m_logger->debug( "Starting Apply step of Migration0To1_0_0" );

        OUTCOME_TRY( auto outDb, InitLegacyDb( basePath_, basePort_ ) );
        OUTCOME_TRY( auto inDb, InitLegacyDb( basePath_, static_cast<uint16_t>( basePort_ + 1 ) ) );

        // m_logger->debug( "Migrating output DB into new DB" );
        OUTCOME_TRY( MigrateDb( outDb, newDb_ ) );

        // m_logger->debug( "Migrating input DB into new DB" );
        OUTCOME_TRY( MigrateDb( inDb, newDb_ ) );

        // m_logger->debug( "Apply step of Migration0To1_0_0 finished successfully" );
        return outcome::success();
    }

} // namespace sgns
