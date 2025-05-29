// MigrationManager.cpp
/**
 * @file MigrationManager.cpp
 * @brief Implementation of MigrationManager and Migration0To1_0_0.
 * @date 2025-05-29
 * @author Luiz (...)
 */
#include "MigrationManager.hpp"
#include <boost/format.hpp>
#include <boost/system/error_code.hpp>

namespace sgns
{
    namespace migration
    {

        void MigrationManager::RegisterStep( std::unique_ptr<IMigrationStep> step )
        {
            steps_.push_back( std::move( step ) );
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
                        OUTCOME_TRY( step->Apply() );
                        version = step->ToVersion();
                        applied = true;
                        break;
                    }
                }
                if ( !applied )
                {
                    return outcome::failure( boost::system::error_code{} );
                }
            }
            return outcome::success();
        }

        // ---- Migration0To1_0_0 ----

        Migration0To1_0_0::Migration0To1_0_0( std::shared_ptr<crdt::GlobalDB>                 newDb,
                                              std::shared_ptr<boost::asio::io_context>        ioContext,
                                              std::shared_ptr<ipfs_pubsub::GossipPubSubTopic> pubSub,
                                              std::shared_ptr<upnp::UPNP>                     upnp,
                                              uint16_t                                        basePort,
                                              const std::string                              &basePath ) :
            newDb_( std::move( newDb ) ),
            ioContext_( std::move( ioContext ) ),
            pubSub_( std::move( pubSub ) ),
            upnp_( std::move( upnp ) ),
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

        outcome::result<std::shared_ptr<crdt::GlobalDB>> Migration0To1_0_0::InitLegacyDb(
            std::shared_ptr<boost::asio::io_context>        ioContext,
            std::shared_ptr<ipfs_pubsub::GossipPubSubTopic> pubSub,
            std::shared_ptr<upnp::UPNP>                     upnp,
            uint16_t                                        port,
            const std::string                              &basePath,
            const std::string                              &suffix )
        {
            std::string path = ( boost::format( "%s_%s" ) % basePath % suffix ).str();
            auto db = std::make_shared<crdt::GlobalDB>( std::move( ioContext ), path, port, std::move( pubSub ) );
            if ( !db->Init( crdt::CrdtOptions::DefaultOptions() ).has_value() )
            {
                return outcome::failure( boost::system::error_code{} );
            }
            return db;
        }

        outcome::result<void> Migration0To1_0_0::MigrateDb( const std::shared_ptr<crdt::GlobalDB> &oldDb,
                                                            const std::shared_ptr<crdt::GlobalDB> &newDb )
        {
            auto maybeEntries = oldDb->QueryKeyValues( "" );
            if ( !maybeEntries.has_value() )
            {
                return outcome::failure( boost::system::error_code{} );
            }
            auto &entries = maybeEntries.value();
            auto  tx      = newDb->BeginTransaction();
            for ( auto &entry : entries )
            {
                auto keyOpt = oldDb->KeyToString( entry.first );
                if ( !keyOpt.has_value() )
                {
                    continue;
                }
                crdt::HierarchicalKey hk( keyOpt.value() );
                OUTCOME_TRY( tx->Put( std::move( hk ), std::move( entry.second ) ) );
            }
            if ( tx->Commit().has_error() )
            {
                return outcome::failure( boost::system::error_code{} );
            }
            return outcome::success();
        }

        outcome::result<void> Migration0To1_0_0::Apply()
        {
            OUTCOME_TRY( auto outDb,
                         InitLegacyDb( std::move( ioContext_ ),
                                       std::move( pubSub_ ),
                                       std::move( upnp_ ),
                                       basePort_,
                                       basePath_,
                                       "out" ) );

            OUTCOME_TRY( auto inDb,
                         InitLegacyDb( std::make_shared<boost::asio::io_context>( *outDb->GetContext() ),
                                       std::make_shared<ipfs_pubsub::GossipPubSubTopic>( *outDb->GetPubSubTopic() ),
                                       std::make_shared<upnp::UPNP>( *upnp_ ),
                                       static_cast<uint16_t>( basePort_ + 1 ),
                                       basePath_,
                                       "in" ) );

            OUTCOME_TRY( MigrateDb( outDb, newDb_ ) );
            OUTCOME_TRY( MigrateDb( inDb, newDb_ ) );
            return outcome::success();
        }

    } // namespace migration
} // namespace sgns
