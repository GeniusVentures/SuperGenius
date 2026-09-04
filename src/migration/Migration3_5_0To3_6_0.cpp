#include "Migration3_5_0To3_6_0.hpp"

#include "MigrationManager.hpp"
#include "account/TransactionManager.hpp"
#include "account/TransferTransaction.hpp"
#include "blockchain/Blockchain.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "base/sgns_version.hpp"

#include <filesystem>
#include <unordered_set>

namespace sgns
{
    Migration3_5_0To3_6_0::Migration3_5_0To3_6_0(
        std::shared_ptr<boost::asio::io_context>                        ioContext,
        std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub,
        std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync,
        std::shared_ptr<libp2p::basic::Scheduler>                       scheduler,
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
        return "3.5.1";
    }

    std::string Migration3_5_0To3_6_0::ToVersion() const
    {
        return "3.6.0";
    }

    outcome::result<bool> Migration3_5_0To3_6_0::IsRequired() const
    {
        if ( !db_3_5_1_ )
        {
            logger_->info( "Legacy 3.5.1 DB not found; skipping migration to {}", ToVersion() );
            return false;
        }

        sgns::crdt::GlobalDB::Buffer version_key;
        version_key.put( std::string( MigrationManager::VERSION_INFO_KEY ) );
        auto version_ret = db_3_6_0_->GetDataStore()->get( version_key );

        if ( version_ret.has_error() )
        {
            logger_->info( "No version info found in GlobalDB, migration from {} to {} is required",
                           FromVersion(),
                           ToVersion() );
            return true;
        }

        auto version_buffer = version_ret.value();

        if ( !IsVersionLessThan( std::string( version_buffer.toString() ), ToVersion() ) )
        {
            logger_->info( "GlobalDB already at target version {}, skipping migration", ToVersion() );
            return false;
        }

        logger_->info( "GlobalDB at version {}, need to migrate", FromVersion() );
        return true;
    }

    outcome::result<void> Migration3_5_0To3_6_0::Init()
    {
        BOOST_OUTCOME_TRY( auto legacy_db, InitLegacyDb() );
        db_3_5_1_ = std::move( legacy_db );
        if ( db_3_5_1_ )
        {
            BOOST_OUTCOME_TRY( auto new_db, InitTargetDb() );
            db_3_6_0_ = std::move( new_db );
        }
        return outcome::success();
    }

    outcome::result<void> Migration3_5_0To3_6_0::Apply()
    {
        if ( !db_3_5_1_ )
        {
            logger_->error( "Legacy 3.5.1 DB not initialized; nothing to migrate to {}", ToVersion() );
            return outcome::success();
        }

        if ( !db_3_6_0_ )
        {
            logger_->error( "Target {} DB not initialized", ToVersion() );
            return outcome::failure( std::errc::invalid_argument );
        }

        logger_->info( "Starting migration from {} to {}", FromVersion(), ToVersion() );

        BOOST_OUTCOME_TRY( ValidatorRegistry::MigrateCids( *db_3_5_1_, *db_3_6_0_ ) );
        BOOST_OUTCOME_TRY( Blockchain::MigrateCids( *db_3_5_1_, *db_3_6_0_ ) );

        auto                            crdt_transaction_ = db_3_6_0_->BeginTransaction();
        std::unordered_set<std::string> topics_;

        topics_.emplace( std::string( TransactionManager::GNUS_FULL_NODES_TOPIC ) );

        std::vector<uint16_t> monitored_networks{ version::DEV_NET_ID, version::TEST_NET_ID, version::MAIN_NET_ID };
        size_t                migrated_count = 0;
        size_t                BATCH_SIZE     = 50;

        for ( const auto network_id : monitored_networks )
        {
            auto blockchain_base = TransactionManager::GetBlockChainBase( network_id );

            std::unordered_set<std::string> unique_keys;
            std::vector<std::string>        transaction_keys;

            BOOST_OUTCOME_TRY( auto entries, db_3_5_1_->QueryKeyValues( blockchain_base, "*", "/tx" ) );
            for ( const auto &entry : entries )
            {
                auto keyOpt = db_3_5_1_->KeyToString( entry.first );
                if ( keyOpt.has_value() && unique_keys.emplace( keyOpt.value() ).second )
                {
                    transaction_keys.push_back( keyOpt.value() );
                }
            }

            logger_->debug( "{}: Found {} transaction keys to migrate on network {}",
                            __func__,
                            transaction_keys.size(),
                            network_id );

            for ( const auto &transaction_key : transaction_keys )
            {
                auto maybe_transaction = TransactionManager::FetchTransaction( *db_3_5_1_, transaction_key );
                if ( !maybe_transaction.has_value() )
                {
                    logger_->error( "Can't fetch transaction for key {}", transaction_key );
                    continue;
                }
                auto &tx = maybe_transaction.value();

                logger_->debug( "{}: Fetched transaction on {}", __func__, transaction_key );
                if ( tx->GetHash().empty() )
                {
                    logger_->error( "{}: NO HASH ON {}", __func__, transaction_key );
                    tx->FillHash();
                }

                const auto new_tx_key = blockchain_base + "tx/" + tx->GetHash();

                logger_->debug( "{}: New TX key {}", __func__, new_tx_key );

                sgns::crdt::GlobalDB::Buffer data_transaction;
                data_transaction.put( tx->SerializeByteVector() );
                BOOST_OUTCOME_TRY( crdt_transaction_->Put( new_tx_key, std::move( data_transaction ) ) );

                topics_.emplace( tx->GetSrcAddress() );
                if ( auto transfer_tx = std::dynamic_pointer_cast<TransferTransaction>( tx ) )
                {
                    for ( const auto &dest_info : transfer_tx->GetDstInfos() )
                    {
                        topics_.emplace( dest_info.dest_address );
                    }
                }

                ++migrated_count;
                if ( migrated_count >= BATCH_SIZE )
                {
                    BOOST_OUTCOME_TRY( crdt_transaction_->Commit( topics_ ) );
                    crdt_transaction_ = db_3_6_0_->BeginTransaction();
                    topics_.clear();
                    topics_.emplace( std::string( TransactionManager::GNUS_FULL_NODES_TOPIC ) );
                    migrated_count = 0;
                    logger_->debug( "Committed a batch of {} transactions", BATCH_SIZE );
                }
            }
        }

        if ( migrated_count )
        {
            BOOST_OUTCOME_TRY( crdt_transaction_->Commit( topics_ ) );
            logger_->debug( "Committed remaining {} transactions", migrated_count );
        }

        sgns::crdt::GlobalDB::Buffer version_buffer;
        sgns::crdt::GlobalDB::Buffer version_key;
        version_key.put( std::string( MigrationManager::VERSION_INFO_KEY ) );
        version_buffer.put( ToVersion() );

        BOOST_OUTCOME_TRY( db_3_6_0_->GetDataStore()->put( version_key, version_buffer ) );
        logger_->debug( "Migration from {} to {} completed successfully", FromVersion(), ToVersion() );

        return outcome::success();
    }

    outcome::result<void> Migration3_5_0To3_6_0::ShutDown()
    {
        db_3_5_1_.reset();
        db_3_6_0_.reset();

        return outcome::success();
    }

    outcome::result<std::shared_ptr<crdt::GlobalDB>> Migration3_5_0To3_6_0::InitLegacyDb() const
    {
        static constexpr std::string_view GNUS_NETWORK_PATH_3_5_1 = "SuperGNUSNode.Node";

        auto full_path = writeBasePath_ + std::string( GNUS_NETWORK_PATH_3_5_1 ) +
                         version::GetNetAndVersionAppendix( 3, 5, version::GetNetworkID() ) + base58key_;

        if ( !std::filesystem::exists( full_path ) )
        {
            logger_->info( "Legacy 3.5.1 DB not found at {}; skipping initialization", full_path );
            return std::shared_ptr<crdt::GlobalDB>{};
        }

        logger_->debug( "Initializing legacy {} DB at path {}", FromVersion(), full_path );

        auto maybe_db_3_5_1 = crdt::GlobalDB::New( ioContext_,
                                                   full_path,
                                                   pubSub_,
                                                   crdt::CrdtOptions::DefaultOptions(),
                                                   graphsync_,
                                                   scheduler_,
                                                   generator_ );

        if ( !maybe_db_3_5_1.has_value() )
        {
            logger_->error( "Legacy {} DB error at path {}", FromVersion(), full_path );
            return outcome::failure( boost::system::error_code{} );
        }

        logger_->debug( "Started legacy {} DB at path {}", FromVersion(), full_path );
        return std::move( maybe_db_3_5_1.value() );
    }

    outcome::result<std::shared_ptr<crdt::GlobalDB>> Migration3_5_0To3_6_0::InitTargetDb() const
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
