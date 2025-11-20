/**
 * @file       Migration3_4_0To3_5_0.cpp
 * @brief      
 * @date       2025-11-14
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#include "Migration3_4_0To3_5_0.hpp"
#include "account/GeniusAccount.hpp"
#include "account/TransactionManager.hpp"

namespace sgns
{
    Migration3_4_0To3_5_0::Migration3_4_0To3_5_0(
        std::shared_ptr<boost::asio::io_context>                        ioContext,
        std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub,
        std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync,
        std::shared_ptr<libp2p::protocol::Scheduler>                    scheduler,
        std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
        std::string                                                     writeBasePath,
        std::string                                                     base58key,
        std::shared_ptr<GeniusAccount>                                  account ) :
        ioContext_( std::move( ioContext ) ),
        pubSub_( std::move( pubSub ) ),
        graphsync_( std::move( graphsync ) ),
        scheduler_( std::move( scheduler ) ),
        generator_( std::move( generator ) ),
        writeBasePath_( std::move( writeBasePath ) ),
        base58key_( std::move( base58key ) ),
        account_( std::move( account ) )
    {
    }

    Migration3_4_0To3_5_0::~Migration3_4_0To3_5_0() {}

    std::string Migration3_4_0To3_5_0::FromVersion() const
    {
        return "3.4.0";
    }

    std::string Migration3_4_0To3_5_0::ToVersion() const
    {
        return "3.5.0";
    }

    outcome::result<bool> Migration3_4_0To3_5_0::IsRequired() const
    {
        bool ret = false;

        do
        {
            sgns::crdt::GlobalDB::Buffer version_key;
            version_key.put( std::string( MigrationManager::VERSION_INFO_KEY ) );
            auto version_ret = db_3_5_0_->GetDataStore()->get( version_key );

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

    outcome::result<void> Migration3_4_0To3_5_0::Init()
    {
        OUTCOME_TRY( auto &&legacy_db, InitLegacyDb() );
        db_3_4_0_ = std::move( legacy_db );
        OUTCOME_TRY( auto &&new_db, InitTargetDb() );
        db_3_5_0_ = std::move( new_db );
        return outcome::success();
    }

    outcome::result<void> Migration3_4_0To3_5_0::Apply()
    {
        logger_->info( "Starting migration from {} to {}", FromVersion(), ToVersion() );

        account_->ConfigureBlockResponseHandler( db_3_5_0_->GetBroadcaster() );

        db_3_5_0_->Start();
        //init blockchain
        if ( !blockchain_ )
        {
            blockchain_ = Blockchain::New(
                db_3_5_0_,
                account_,
                [wptr( weak_from_this() )]( outcome::result<void> result )
                {
                    if ( auto strong = wptr.lock() )
                    {
                        if ( result.has_error() )
                        {
                            strong->logger_->error( "Error starting blockchain: {}", result.error().message() );
                            strong->blockchain_status_.store( Status::ERROR );
                            return;
                        }
                        strong->logger_->debug( "Blockchain started successfully, starting transaction manager" );
                        strong->blockchain_status_.store( Status::SUCCESS );
                    }
                } );
            blockchain_->Start();
        }
        auto timeout_duration     = std::chrono::minutes( 4 );
        auto start_time           = std::chrono::steady_clock::now();
        auto last_log_time        = start_time;
        bool blockchain_succeeded = false;

        while ( std::chrono::steady_clock::now() - start_time < timeout_duration )
        {
            auto current_time = std::chrono::steady_clock::now();
            if ( blockchain_status_.load( std::memory_order_acquire ) != Status::INIT )
            {
                // spin or sleep
                if ( blockchain_status_.load( std::memory_order_acquire ) == Status::SUCCESS )
                {
                    auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>( current_time - start_time )
                                               .count();
                    blockchain_succeeded = true;
                    logger_->debug( "{}: Blockchain succeeded (elapsed: {}s)", __func__, elapsed_seconds );
                }
                break;
            }
            if ( current_time - last_log_time >= std::chrono::seconds( 30 ) )
            {
                auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>( current_time - start_time )
                                           .count();
                logger_->info( "{}: Still waiting for the blockchain to initialize (elapsed: {}s)",
                               __func__,
                               elapsed_seconds );
                last_log_time = current_time;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }
        if ( !blockchain_succeeded )
        {
            auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>( std::chrono::steady_clock::now() -
                                                                                     start_time )
                                       .count();
            logger_->error( "{}: Blockchain errored out (elasped {}s)", __func__, elapsed_seconds );

            return outcome::failure( boost::system::error_code{} );
        }
        auto                  crdt_transaction_ = db_3_5_0_->BeginTransaction();
        std::set<std::string> topics_;

        topics_.emplace( std::string( TransactionManager::GNUS_FULL_NODES_TOPIC ) );

        std::vector<uint16_t> monitored_networks{ version::DEV_NET_ID, version::TEST_NET_ID, version::MAIN_NET_ID };
        size_t                migrated_count = 0;
        size_t                BATCH_SIZE     = 50;

        for ( const auto network_id : monitored_networks )
        {
            auto        blockchain_base = TransactionManager::GetBlockChainBase( network_id );
            OUTCOME_TRY( auto &&entries, db_3_4_0_->QueryKeyValues( blockchain_base, "*", "/tx" ) );
            logger_->debug( "Found {} transaction keys to migrate for network {}", entries.size(), network_id );

            for ( const auto &entry : entries )
            {
                auto keyOpt = db_3_4_0_->KeyToString( entry.first );
                if ( !keyOpt.has_value() )
                {
                    logger_->error( "Failed to convert key buffer to string" );
                    continue;
                }
                std::string transaction_key   = keyOpt.value();
                auto        maybe_transaction = TransactionManager::FetchTransaction( db_3_4_0_, transaction_key );
                if ( !maybe_transaction.has_value() )
                {
                    logger_->error( "Can't fetch transaction for key {}", transaction_key );
                    continue;
                }
                auto tx = maybe_transaction.value();
                logger_->trace( "Fetched transaction {}", transaction_key );

                if ( !tx->CheckSignature() )
                {
                    if ( !tx->CheckDAGSignatureLegacy() )
                    {
                        logger_->error( "Could not validate signature of transaction {}", transaction_key );
                        continue;
                    }
                }

                topics_.emplace( tx->GetSrcAddress() );
                if ( auto transfer_tx = std::dynamic_pointer_cast<TransferTransaction>( tx ) )
                {
                    for ( const auto &dest_info : transfer_tx->GetDstInfos() )
                    {
                        topics_.emplace( dest_info.dest_address );
                    }
                }
                if ( auto escrow_tx = std::dynamic_pointer_cast<EscrowReleaseTransaction>( tx ) )
                {
                    topics_.emplace( escrow_tx->GetSrcAddress() );
                    topics_.emplace( escrow_tx->GetEscrowSource() );
                }

                sgns::crdt::GlobalDB::Buffer data_transaction;
                data_transaction.put( tx->SerializeByteVector() );
                BOOST_OUTCOME_TRYV2( auto &&, crdt_transaction_->Put( transaction_key, std::move( data_transaction ) ) );

                ++migrated_count;
                if ( migrated_count >= BATCH_SIZE )
                {
                    OUTCOME_TRY( crdt_transaction_->Commit( topics_ ) );
                    crdt_transaction_ = db_3_5_0_->BeginTransaction(); // start fresh
                    topics_.clear();

                    topics_.emplace( std::string( TransactionManager::GNUS_FULL_NODES_TOPIC ) );
                    migrated_count = 0;
                    logger_->debug( "Committed a batch of {} transactions", BATCH_SIZE );
                }
            }
        }
        if ( migrated_count )
        {
            OUTCOME_TRY( crdt_transaction_->Commit( topics_ ) );
            logger_->debug( "Committed remaining {}  transactions", migrated_count );
        }

        sgns::crdt::GlobalDB::Buffer version_buffer;
        sgns::crdt::GlobalDB::Buffer version_key;
        version_key.put( std::string( MigrationManager::VERSION_INFO_KEY ) );
        version_buffer.put( ToVersion() );

        OUTCOME_TRY( db_3_5_0_->GetDataStore()->put( version_key, version_buffer ) );
        logger_->debug( "Migration from {} to {} completed successfully", FromVersion(), ToVersion() );

        return outcome::success();
    }

    outcome::result<std::shared_ptr<crdt::GlobalDB>> Migration3_4_0To3_5_0::InitLegacyDb()
    {
        static constexpr std::string_view GNUS_NETWORK_PATH_3_4_0 = "SuperGNUSNode.Node";

        auto full_path = writeBasePath_ + std::string( GNUS_NETWORK_PATH_3_4_0 ) +
                         version::GetNetAndVersionAppendix( 3, 4, version::GetNetworkID() ) + base58key_;

        logger_->debug( "Initializing legacy {} DB at path {}", ToVersion(), full_path );

        auto maybe_db_3_4_0 = crdt::GlobalDB::New( ioContext_,
                                                   full_path,
                                                   pubSub_,
                                                   crdt::CrdtOptions::DefaultOptions(),
                                                   graphsync_,
                                                   scheduler_,
                                                   generator_ );

        if ( !maybe_db_3_4_0.has_value() )
        {
            logger_->error( "Legacy {} DB error at path {}", ToVersion(), full_path );
            return outcome::failure( boost::system::error_code{} );
        }

        logger_->debug( "Started legacy {} DB at path {}", ToVersion(), full_path );
        return std::move( maybe_db_3_4_0.value() );
    }

    outcome::result<std::shared_ptr<crdt::GlobalDB>> Migration3_4_0To3_5_0::InitTargetDb()
    {
        static constexpr std::string_view GNUS_NETWORK_PATH_3_5_0 = "SuperGNUSNode.Node";

        auto full_path = writeBasePath_ + std::string( GNUS_NETWORK_PATH_3_5_0 ) +
                         version::GetNetAndVersionAppendix( 3, 5, version::GetNetworkID() ) + base58key_;

        logger_->debug( "Initializing target {} DB at path {}", ToVersion(), full_path );

        auto maybe_db_3_5_0 = crdt::GlobalDB::New( ioContext_,
                                                   full_path,
                                                   pubSub_,
                                                   crdt::CrdtOptions::DefaultOptions(),
                                                   graphsync_,
                                                   scheduler_,
                                                   generator_ );

        if ( !maybe_db_3_5_0.has_value() )
        {
            logger_->error( "Target {} DB error at path {}", ToVersion(), full_path );
            return outcome::failure( boost::system::error_code{} );
        }

        logger_->debug( "Started target {} DB at path {}", ToVersion(), full_path );
        return std::move( maybe_db_3_5_0.value() );
    }

    outcome::result<void> Migration3_4_0To3_5_0::ShutDown()
    {
        db_3_4_0_.reset();
        db_3_5_0_.reset();
        blockchain_.reset();
        return outcome::success();
    }
}
