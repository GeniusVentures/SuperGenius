#include "Migration3_5_0To3_6_0.hpp"

#include "base/sgns_version.hpp"
#include "account/TransactionManager.hpp"
#include "account/MigrationManager.hpp"

namespace sgns
{
    Migration3_5_0To3_6_0::Migration3_5_0To3_6_0(
        std::shared_ptr<boost::asio::io_context>                        ioContext,
        std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub,
        std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync,
        std::shared_ptr<libp2p::protocol::Scheduler>                    scheduler,
        std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
        std::string                                                     writeBasePath,
        std::string                                                     base58key ) :
        ioContext_( std::move( ioContext ) ),
        pubSub_( std::move( pubSub ) ),
        graphsync_( std::move( graphsync ) ),
        scheduler_( std::move( scheduler ) ),
        generator_( std::move( generator ) ),
        writeBasePath_( std::move( writeBasePath ) ),
        base58key_( std::move( base58key ) )
    {
    }

    std::string Migration3_5_0To3_6_0::FromVersion() const
    {
        return "3.5.0";
    }

    std::string Migration3_5_0To3_6_0::ToVersion() const
    {
        return "3.6.0";
    }

    outcome::result<void> Migration3_5_0To3_6_0::Init()
    {
        OUTCOME_TRY( auto &&legacy_db, InitLegacyDb() );
        db_3_5_0_ = std::move( legacy_db );
        if ( db_3_5_0_ )
        {
            json_storage_ = std::make_shared<JSONSecureStorage>( writeBasePath_ );

            OUTCOME_TRY( auto &&new_db, InitTargetDb() );
            db_3_6_0_ = std::move( new_db );

            secure_storage_ = std::make_shared<SecureStorageImpl>( writeBasePath_ );
        }
        return outcome::success();
    }

    outcome::result<void> Migration3_5_0To3_6_0::Apply()
    {
        auto old_json = json_storage_->LoadJSON();

        if (old_json.has_error()) {
            if (old_json.error() == std::errc::no_such_file_or_directory) {
                logger_->debug("There were no legacy JSON storage to migrate from");
                return outcome::success();
            }
            logger_->error("Could not load legacy JSON storage at {}", this->writeBasePath_);
            return old_json.as_failure();
        }

        auto maybe_field = old_json.value().FindMember( "GeniusAccount" );

        if ( maybe_field == old_json.value().MemberEnd() || !maybe_field->value.IsObject() )
        {
            logger_->error( "Failed to find GeniusAccount member in old JSON storage" );
            return outcome::failure( std::errc::bad_message );
        }

        rj::Document new_doc;
        new_doc.CopyFrom( maybe_field->value, new_doc.GetAllocator() );

        return secure_storage_->SaveJSON( std::move( new_doc ) );
    }

    outcome::result<void> Migration3_5_0To3_6_0::ShutDown()
    {
        db_3_5_0_.reset();
        db_3_6_0_.reset();
        json_storage_.reset();
        secure_storage_.reset();

        return outcome::success();
    }

    outcome::result<bool> Migration3_5_0To3_6_0::IsRequired() const
    {
        bool ret = false;

        if ( !db_3_5_0_ )
        {
            logger_->info( "Legacy 3.5.0 DB not found; skipping migration to {}", ToVersion() );
            return false;
        }

        do
        {
            sgns::crdt::GlobalDB::Buffer version_key;
            version_key.put( std::string( MigrationManager::VERSION_INFO_KEY ) );
            auto version_ret = db_3_6_0_->GetDataStore()->get( version_key );

            if ( version_ret.has_error() )
            {
                // No version info found, migration is required
                logger_->info( "No version info found in GlobalDB, migration from {} to {} is required",
                               FromVersion(),
                               ToVersion() );
                ret = true;
                break;
            }
            auto version_buffer = version_ret.value();

            if ( !IsVersionLessThan( std::string( version_buffer.toString() ), ToVersion() ) )
            {
                logger_->info( "GlobalDB already at target version {}, skipping migration", ToVersion() );
                break;
            }
            logger_->info( "GlobalDB at version {}, need to migrate", FromVersion(), ToVersion() );
            ret = true;

        } while ( 0 );

        return ret;
    }

    outcome::result<std::shared_ptr<crdt::GlobalDB>> Migration3_5_0To3_6_0::InitLegacyDb()
    {
        static constexpr std::string_view GNUS_NETWORK_PATH_3_5_0 = "SuperGNUSNode.Node";

        auto full_path = writeBasePath_ + std::string( GNUS_NETWORK_PATH_3_5_0 ) +
                         version::GetNetAndVersionAppendix( 3, 5, version::GetNetworkID() ) + base58key_;

        if ( !std::filesystem::exists( full_path ) )
        {
            logger_->info( "Legacy 3.5.0 DB not found at {}; skipping initialization", full_path );
            return std::shared_ptr<crdt::GlobalDB>{};
        }

        logger_->debug( "Initializing legacy {} DB at path {}", FromVersion(), full_path );

        auto maybe_db_3_5_0 = crdt::GlobalDB::New( ioContext_,
                                                   full_path,
                                                   pubSub_,
                                                   crdt::CrdtOptions::DefaultOptions(),
                                                   graphsync_,
                                                   scheduler_,
                                                   generator_ );

        if ( !maybe_db_3_5_0.has_value() )
        {
            logger_->error( "Legacy {} DB error at path {}", FromVersion(), full_path );
            return outcome::failure( boost::system::error_code{} );
        }

        logger_->debug( "Started legacy {} DB at path {}", FromVersion(), full_path );
        return std::move( maybe_db_3_5_0.value() );
    }

    outcome::result<std::shared_ptr<crdt::GlobalDB>> Migration3_5_0To3_6_0::InitTargetDb()
    {
        static constexpr std::string_view GNUS_NETWORK_PATH_3_6_0 = "SuperGNUSNode.Node";

        auto full_path = writeBasePath_ + std::string( GNUS_NETWORK_PATH_3_6_0 ) +
                         version::GetNetAndVersionAppendix( 3, 6, version::GetNetworkID() ) + base58key_;

        logger_->debug( "Initializing target {} DB at path {}", ToVersion(), full_path );

        auto maybe_db_3_6_0 = crdt::GlobalDB::New( ioContext_,
                                                   full_path,
                                                   pubSub_,
                                                   crdt::CrdtOptions::DefaultOptions(),
                                                   graphsync_,
                                                   scheduler_,
                                                   generator_ );

        if ( !maybe_db_3_6_0.has_value() )
        {
            logger_->error( "Target {} DB error at path {}", ToVersion(), full_path );
            return outcome::failure( boost::system::error_code{} );
        }

        logger_->debug( "Started target {} DB at path {}", ToVersion(), full_path );
        return std::move( maybe_db_3_6_0.value() );
    }
}
