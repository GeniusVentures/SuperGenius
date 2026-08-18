/**
 * @file       Migration3_4_0To3_5_0.cpp
 * @brief
 * @date       2025-11-14
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#include "Migration3_4_0To3_5_0.hpp"

#include "account/GeniusAccount.hpp"
#include "account/MintTransaction.hpp"
#include "account/TransactionManager.hpp"
#include "MigrationManager.hpp"
#include "account/TransferTransaction.hpp"
#include "blockchain/ValidatorRegistry.hpp"

namespace sgns
{

    namespace
    {
        std::string BuildLegacyTransactionPath_3_5_0( const GeniusTransaction &tx )
        {
            return tx.GetSrcAddress() + "/tx/" + tx.GetTransactionSpecificPath() + "/" +
                   std::to_string( tx.dag_st.nonce() );
        }
    }

    Migration3_4_0To3_5_0::Migration3_4_0To3_5_0(
        std::shared_ptr<boost::asio::io_context>                        ioContext,
        std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub,
        std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync,
        std::shared_ptr<libp2p::basic::Scheduler>                       scheduler,
        std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
        std::string                                                     writeBasePath,
        std::string                                                     base58key,
        std::shared_ptr<GeniusAccount>                                  account,
        NodeType                                                        node_type ) :
        ioContext_( std::move( ioContext ) ),
        pubSub_( std::move( pubSub ) ),
        graphsync_( std::move( graphsync ) ),
        scheduler_( std::move( scheduler ) ),
        generator_( std::move( generator ) ),
        writeBasePath_( std::move( writeBasePath ) ),
        base58key_( std::move( base58key ) ),
        account_( std::move( account ) ),
        node_type_( node_type )
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

        if ( !db_3_4_0_ )
        {
            logger_->info( "Legacy 3.4.0 DB not found; skipping migration to {}", ToVersion() );
            return ret;
        }

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
        BOOST_OUTCOME_TRY( auto legacy_db, InitLegacyDb() );
        db_3_4_0_ = std::move( legacy_db );
        if ( db_3_4_0_ )
        {
            BOOST_OUTCOME_TRY( auto new_db, InitTargetDb() );
            db_3_5_0_ = std::move( new_db );
        }
        return outcome::success();
    }

    outcome::result<void> Migration3_4_0To3_5_0::Apply()
    {
        if ( !db_3_4_0_ )
        {
            logger_->error( "Legacy 3.4.0 DB not initialized; nothing to migrate to {}", ToVersion() );
            return outcome::success();
        }

        logger_->info( "Starting migration from {} to {}", FromVersion(), ToVersion() );

        account_->ConfigureDatabaseDependencies( db_3_5_0_ );

        db_3_5_0_->StartCICSync();
        logger_->info( "Broadcast suppression enabled for migration target DB" );

        //init blockchain
        if ( !blockchain_ )
        {
            blockchain_ = Blockchain::New(
                db_3_5_0_,
                account_,
                pubSub_,
                [wptr( weak_from_this() )]( outcome::result<void> result )
                {
                    if ( auto strong = wptr.lock() )
                    {
                        if ( result.has_error() )
                        {
                            strong->logger_->error( "Error starting blockchain: {}", result.error().message() );
                            strong->blockchain_status_.store( Status::ST_ERROR );
                            return;
                        }
                        strong->logger_->debug( "Blockchain started successfully, starting transaction manager" );
                        strong->blockchain_status_.store( Status::ST_SUCCESS );
                    }
                },
                node_type_ );
        }

        auto                  retry_duration   = std::chrono::minutes( 2 );
        auto                  retry_interval   = std::chrono::seconds( 5 );
        auto                  retry_start_time = std::chrono::steady_clock::now();
        auto                  last_log_time    = retry_start_time;
        outcome::result<void> start_result     = outcome::failure( Blockchain::Error::BLOCKCHAIN_NOT_INITIALIZED );
        do
        {
            start_result = blockchain_->Start();
            if ( start_result.has_error() )
            {
                logger_->error( "Error starting blockchain: {}", start_result.error().message() );
            }

            auto current_time = std::chrono::steady_clock::now();
            if ( current_time - last_log_time >= std::chrono::seconds( 30 ) )
            {
                auto elapsed_seconds =
                    std::chrono::duration_cast<std::chrono::seconds>( current_time - retry_start_time ).count();
                logger_->info( "{}: Retrying blockchain start (elapsed: {}s)", __func__, elapsed_seconds );
                last_log_time = current_time;
            }
            std::this_thread::sleep_for( retry_interval );
        } while ( std::chrono::steady_clock::now() - retry_start_time < retry_duration && start_result.has_error() );

        auto timeout_duration     = std::chrono::minutes( 4 );
        auto start_time           = std::chrono::steady_clock::now();
        last_log_time             = start_time;
        bool blockchain_succeeded = false;

        while ( std::chrono::steady_clock::now() - start_time < timeout_duration )
        {
            auto current_time = std::chrono::steady_clock::now();
            if ( blockchain_status_.load( std::memory_order_acquire ) != Status::ST_INIT )
            {
                // spin or sleep
                if ( blockchain_status_.load( std::memory_order_acquire ) == Status::ST_SUCCESS )
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
            logger_->error( "{}: Blockchain errored out (elapsed {}s)", __func__, elapsed_seconds );

            return outcome::failure( MigrationManager::Error::BLOCKCHAIN_INIT_FAILED );
        }

        start_time           = std::chrono::steady_clock::now();
        blockchain_succeeded = false;
        while ( std::chrono::steady_clock::now() - start_time < timeout_duration )
        {
            auto genesis_cid_result          = blockchain_->GetGenesisCID();
            auto account_creation_cid_result = blockchain_->GetAccountCreationCID();
            if ( genesis_cid_result.has_value() && account_creation_cid_result.has_value() )
            {
                blockchain_succeeded = true;
                break;
            }

            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }
        if ( !blockchain_succeeded )
        {
            logger_->error( "{}: Genesis and/or Account Creation CID not fetched", __func__ );

            return outcome::failure( MigrationManager::Error::BLOCKCHAIN_INIT_FAILED );
        }

        auto                            crdt_transaction_ = db_3_5_0_->BeginTransaction();
        std::unordered_set<std::string> topics_;

        topics_.emplace( std::string( TransactionManager::GNUS_FULL_NODES_TOPIC ) );

        std::vector<uint16_t> monitored_networks{ version::DEV_NET_ID, version::TEST_NET_ID, version::MAIN_NET_ID };
        size_t                migrated_count = 0;
        size_t                BATCH_SIZE     = 50;

        struct TransactionRecord
        {
            std::shared_ptr<GeniusTransaction> tx;
            std::string                          key;
        };

        for ( const auto network_id : monitored_networks )
        {
            auto blockchain_base = TransactionManager::GetBlockChainBase( network_id );
            BOOST_OUTCOME_TRY( auto entries, db_3_4_0_->QueryKeyValues( blockchain_base, "*", "/tx" ) );
            logger_->debug( "Found {} transaction keys to migrate on network {}", entries.size(), network_id );

            std::vector<TransactionRecord> owned_transactions;
            owned_transactions.reserve( entries.size() );
            std::vector<TransactionRecord> other_transactions;
            other_transactions.reserve( entries.size() );

            auto persist_record = [&]( const TransactionRecord &record ) -> outcome::result<void>
            {
                sgns::crdt::GlobalDB::Buffer data_transaction;
                data_transaction.put( record.tx->SerializeByteVector() );
                BOOST_OUTCOME_TRY( crdt_transaction_->Put( record.key, std::move( data_transaction ) ) );

                topics_.emplace( record.tx->GetSrcAddress() );
                if ( auto transfer_tx = std::dynamic_pointer_cast<TransferTransaction>( record.tx ) )
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
                    crdt_transaction_ = db_3_5_0_->BeginTransaction();
                    topics_.clear();
                    topics_.emplace( std::string( TransactionManager::GNUS_FULL_NODES_TOPIC ) );
                    migrated_count = 0;
                    logger_->debug( "Committed a batch of {} transactions", BATCH_SIZE );
                }
                return outcome::success();
            };

            for ( const auto &entry : entries )
            {
                auto keyOpt = db_3_4_0_->KeyToString( entry.first );
                if ( !keyOpt.has_value() )
                {
                    logger_->error( "Failed to convert key buffer to string" );
                    continue;
                }
                std::string transaction_key   = keyOpt.value();
                auto        maybe_transaction = TransactionManager::FetchTransaction( *db_3_4_0_, transaction_key );
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

                bool              is_owned = ( tx->GetSrcAddress() == account_->GetAddress() );
                TransactionRecord record{ tx, std::move( transaction_key ) };

                if ( is_owned )
                {
                    owned_transactions.push_back( std::move( record ) );
                }
                else
                {
                    other_transactions.push_back( std::move( record ) );
                }
            }

            std::sort( owned_transactions.begin(),
                       owned_transactions.end(),
                       []( const TransactionRecord &lhs, const TransactionRecord &rhs )
                       {
                           if ( lhs.tx->dag_st.nonce() == rhs.tx->dag_st.nonce() )
                           {
                               return lhs.key < rhs.key;
                           }
                           return lhs.tx->dag_st.nonce() < rhs.tx->dag_st.nonce();
                       } );

            if ( !owned_transactions.empty() )
            {
                uint64_t expected_nonce = 0;
                uint64_t last_timestamp = 0;

                auto create_zero_value_mint = [&]( uint64_t nonce ) -> TransactionRecord
                {
                    SGTransaction::DAGStruct dag;
                    dag.set_previous_hash( "" );
                    dag.set_nonce( nonce );
                    dag.set_source_addr( account_->GetAddress() );
                    auto current_timestamp = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch() )
                            .count() );
                    if ( last_timestamp != 0 && current_timestamp <= last_timestamp )
                    {
                        current_timestamp = last_timestamp + 1;
                    }
                    dag.set_timestamp( current_timestamp );
                    dag.set_uncle_hash( "" );
                    dag.set_data_hash( "" );

                    TokenID token_id;
                    auto    mint_tx = std::make_shared<MintTransaction>(
                        MintTransaction::New( 0, std::to_string( network_id ), token_id, std::move( dag ) ) );
                    mint_tx->MakeSignature( *account_ );

                    logger_->info( "Synthesizing zero-value mint for missing nonce {} on network {}",
                                   nonce,
                                   network_id );
                    auto tx_path = blockchain_base + BuildLegacyTransactionPath_3_5_0( *mint_tx );
                    return TransactionRecord{ std::move( mint_tx ), std::move( tx_path ) };
                };

                for ( auto &record : owned_transactions )
                {
                    while ( expected_nonce < record.tx->dag_st.nonce() )
                    {
                        auto filler_record = create_zero_value_mint( expected_nonce );
                        logger_->info( "Synthesized zero-value mint for missing nonce {} on network {}",
                                       expected_nonce,
                                       network_id );
                        BOOST_OUTCOME_TRY( persist_record( filler_record ) );
                        last_timestamp = filler_record.tx->GetTimestamp();
                        ++expected_nonce;
                    }

                    BOOST_OUTCOME_TRY( persist_record( record ) );
                    last_timestamp = record.tx->GetTimestamp();
                    expected_nonce = record.tx->dag_st.nonce() + 1;
                }
            }

            for ( const auto &record : other_transactions )
            {
                BOOST_OUTCOME_TRY( persist_record( record ) );
            }
        }
        if ( migrated_count != 0 )
        {
            BOOST_OUTCOME_TRY( crdt_transaction_->Commit( topics_ ) );
            logger_->debug( "Committed remaining {}  transactions", migrated_count );
        }

        sgns::crdt::GlobalDB::Buffer version_buffer;
        sgns::crdt::GlobalDB::Buffer version_key;
        version_key.put( std::string( MigrationManager::VERSION_INFO_KEY ) );
        version_buffer.put( ToVersion() );

        BOOST_OUTCOME_TRY( db_3_5_0_->GetDataStore()->put( version_key, version_buffer ) );
        logger_->debug( "Migration from {} to {} completed successfully", FromVersion(), ToVersion() );

        return outcome::success();
    }

    outcome::result<std::shared_ptr<crdt::GlobalDB>> Migration3_4_0To3_5_0::InitLegacyDb()
    {
        static constexpr std::string_view GNUS_NETWORK_PATH_3_4_0 = "SuperGNUSNode.Node";

        auto full_path = writeBasePath_ + std::string( GNUS_NETWORK_PATH_3_4_0 ) +
                         version::GetNetAndVersionAppendix( 3, 4, version::GetNetworkID() ) + base58key_;

        if ( !std::filesystem::exists( full_path ) )
        {
            logger_->info( "Legacy 3.4.0 DB not found at {}; skipping initialization", full_path );
            return std::shared_ptr<crdt::GlobalDB>{};
        }

        logger_->debug( "Initializing legacy {} DB at path {}", FromVersion(), full_path );

        auto maybe_db_3_4_0 = crdt::GlobalDB::New( ioContext_,
                                                   full_path,
                                                   pubSub_,
                                                   crdt::CrdtOptions::DefaultOptions(),
                                                   graphsync_,
                                                   scheduler_,
                                                   generator_ );

        if ( !maybe_db_3_4_0.has_value() )
        {
            logger_->error( "Legacy {} DB error at path {}", FromVersion(), full_path );
            return outcome::failure( boost::system::error_code{} );
        }

        logger_->debug( "Started legacy {} DB at path {}", FromVersion(), full_path );
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
        account_->DeconfigureDatabaseDependencies();
        db_3_4_0_.reset();
        db_3_5_0_.reset();
        blockchain_.reset();
        return outcome::success();
    }
}
