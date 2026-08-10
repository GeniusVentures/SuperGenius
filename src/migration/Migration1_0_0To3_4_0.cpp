/**
 * @file       Migration1_0_0To3_4_0.cpp
 * @brief
 * @date       2025-10-03
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "Migration1_0_0To3_4_0.hpp"

#include <filesystem>

#include "MigrationManager.hpp"
#include "account/TransactionManager.hpp"
#include "account/TransferTransaction.hpp"
#include "base/sgns_version.hpp"

namespace sgns
{
    namespace
    {
        std::string BuildLegacyProofPath_1_0_0( const GeniusTransaction &tx )
        {
            return tx.GetSrcAddress() + "/proof/" + tx.GetTransactionSpecificPath() + "/" +
                   std::to_string( tx.dag_st.nonce() );
        }
    }

    Migration1_0_0To3_4_0::Migration1_0_0To3_4_0(
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
        if ( !db_1_0_0_ )
        {
            logger_->info( "Legacy 1.0.0 DB not found; skipping migration to {}", ToVersion() );
            return false;
        }

        sgns::crdt::GlobalDB::Buffer version_key;
        version_key.put( std::string( MigrationManager::VERSION_INFO_KEY ) );
        auto version_ret = db_3_4_0_->GetDataStore()->get( version_key );

        if ( version_ret.has_error() )
        {
            // No version info found, migration is required
            logger_->info( "No version info found in GlobalDB, migration from {} to {} is required",
                           FromVersion(),
                           ToVersion() );
            return true;
        }

        auto version_buffer = version_ret.value();

        if ( !IsVersionLessThan( std::string( version_buffer.toString() ), ToVersion() ) )
        {
            logger_->info( "GlobalDB already at target version {}, skipping migration", ToVersion() );
            return false; // Already at target version
        }

        logger_->info( "GlobalDB at version {}, ", ToVersion() );
        return true; // Migration is not required, 1_0_0 is already fixed
    }

    outcome::result<void> Migration1_0_0To3_4_0::Init()
    {
        BOOST_OUTCOME_TRY( auto legacy_db, InitLegacyDb() );
        db_1_0_0_ = std::move( legacy_db );
        if ( db_1_0_0_ )
        {
            BOOST_OUTCOME_TRY( auto new_db, InitTargetDb() );
            db_3_4_0_ = std::move( new_db );
        }
        return outcome::success();
    }

    outcome::result<void> Migration1_0_0To3_4_0::Apply()
    {
        if ( !db_1_0_0_ )
        {
            logger_->error( "Legacy 1.0.0 DB not initialized; nothing to migrate to {}", ToVersion() );
            return outcome::success();
        }

        logger_->info( "Starting migration from {} to {}", FromVersion(), ToVersion() );
        auto                            crdt_transaction_ = db_3_4_0_->BeginTransaction();
        std::unordered_set<std::string> topics_;

        topics_.emplace( TransactionManager::GNUS_FULL_NODES_TOPIC );

        const std::string BASE = "/bc-963/";
        BOOST_OUTCOME_TRY( auto entries, db_1_0_0_->QueryKeyValues( BASE, "*", "/tx" ) );
        logger_->debug( "Found {} transaction keys to migrate", entries.size() );
        size_t migrated_count = 0;
        size_t BATCH_SIZE     = 50;
        for ( const auto &entry : entries )
        {
            auto keyOpt = db_1_0_0_->KeyToString( entry.first );
            if ( !keyOpt.has_value() )
            {
                logger_->error( "Failed to convert key buffer to string" );
                continue;
            }
            std::string transaction_key   = keyOpt.value();
            auto        maybe_transaction = TransactionManager::FetchTransaction( *db_1_0_0_, transaction_key );
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
            auto maybe_proof = db_1_0_0_->Get( { BASE + BuildLegacyProofPath_1_0_0( *tx ) } );

            if ( !maybe_proof.has_value() )
            {
                logger_->error( "Can't find the proof data for {}", transaction_key );
                continue;
            }

            topics_.emplace( tx->GetSrcAddress() );
            if ( auto transfer_tx = std::dynamic_pointer_cast<TransferTransaction>( tx ) )
            {
                for ( const auto &dest_info : transfer_tx->GetDstInfos() )
                {
                    topics_.emplace( dest_info.dest_address );
                }
            }

            sgns::crdt::GlobalDB::Buffer data_transaction;
            data_transaction.put( tx->SerializeByteVector() );
            BOOST_OUTCOME_TRY( crdt_transaction_->Put( transaction_key, std::move( data_transaction ) ) );

            sgns::crdt::HierarchicalKey  proof_crdt_key( BASE + BuildLegacyProofPath_1_0_0( *tx ) );
            sgns::crdt::GlobalDB::Buffer proof_transaction;
            proof_transaction.put( maybe_proof.value() );
            BOOST_OUTCOME_TRYV2(
                auto &&,
                crdt_transaction_->Put( std::move( proof_crdt_key ), std::move( proof_transaction ) ) );
            logger_->trace( "Proof recorded for transaction {}", transaction_key );

            ++migrated_count;
            if ( migrated_count >= BATCH_SIZE )
            {
                BOOST_OUTCOME_TRY( crdt_transaction_->Commit( topics_ ) );
                crdt_transaction_ = db_3_4_0_->BeginTransaction(); // start fresh
                topics_.clear();

                topics_.emplace( std::string( TransactionManager::GNUS_FULL_NODES_TOPIC ) );
                migrated_count = 0;
                logger_->debug( "Committed a batch of {} transactions", BATCH_SIZE );
            }
        }
        if ( migrated_count )
        {
            BOOST_OUTCOME_TRY( crdt_transaction_->Commit( topics_ ) );
            logger_->debug( "Committed remaining {}  transactions", migrated_count );
        }

        sgns::crdt::GlobalDB::Buffer version_buffer;
        sgns::crdt::GlobalDB::Buffer version_key;
        version_key.put( std::string( MigrationManager::VERSION_INFO_KEY ) );
        version_buffer.put( ToVersion() );

        BOOST_OUTCOME_TRY( db_3_4_0_->GetDataStore()->put( version_key, version_buffer ) );
        logger_->debug( "Migration from {} to {} completed successfully", FromVersion(), ToVersion() );

        return outcome::success();
    }

    outcome::result<std::shared_ptr<crdt::GlobalDB>> Migration1_0_0To3_4_0::InitLegacyDb()
    {
        static constexpr auto LEGACY_PREFIX_FMT = "SuperGNUSNode.TestNet.2a.01.%1%";

        const auto legacyNetworkFullPath = ( boost::format( LEGACY_PREFIX_FMT ) % base58key_ ).str();
        const auto fullPath              = ( boost::format( "%s%s" ) % writeBasePath_ % legacyNetworkFullPath ).str();

        if ( !std::filesystem::exists( fullPath ) )
        {
            logger_->info( "Legacy 1.0.0 DB not found at {}; skipping initialization", fullPath );
            return std::shared_ptr<crdt::GlobalDB>{};
        }

        logger_->debug( "Initializing legacy DB at path {}", fullPath );

        auto maybe_db_1_0_0 = crdt::GlobalDB::New( ioContext_,
                                                   fullPath,
                                                   pubSub_,
                                                   crdt::CrdtOptions::DefaultOptions(),
                                                   graphsync_,
                                                   scheduler_,
                                                   generator_ );

        if ( !maybe_db_1_0_0.has_value() )
        {
            logger_->error( "Legacy DB error at path {}", fullPath );
            return outcome::failure( boost::system::error_code{} );
        }

        logger_->debug( "Started legacy DB at path {}", fullPath );
        return std::move( maybe_db_1_0_0.value() );
    }

    outcome::result<std::shared_ptr<crdt::GlobalDB>> Migration1_0_0To3_4_0::InitTargetDb()
    {
        static constexpr std::string_view GNUS_NETWORK_PATH_3_4_0 = "SuperGNUSNode.Node";

        auto full_path = writeBasePath_ + std::string( GNUS_NETWORK_PATH_3_4_0 ) +
                         version::GetNetAndVersionAppendix( 3, 4, version::GetNetworkID() ) + base58key_;

        logger_->debug( "Initializing target {} DB at path {}", ToVersion(), full_path );

        auto maybe_db_3_4_0 = crdt::GlobalDB::New( ioContext_,
                                                   full_path,
                                                   pubSub_,
                                                   crdt::CrdtOptions::DefaultOptions(),
                                                   graphsync_,
                                                   scheduler_,
                                                   generator_ );

        if ( !maybe_db_3_4_0.has_value() )
        {
            logger_->error( "Target {} DB error at path {}", ToVersion(), full_path );
            return outcome::failure( boost::system::error_code{} );
        }

        logger_->debug( "Started target {} DB at path {}", ToVersion(), full_path );
        return std::move( maybe_db_3_4_0.value() );
    }

    outcome::result<void> Migration1_0_0To3_4_0::ShutDown()
    {
        db_1_0_0_.reset();
        db_3_4_0_.reset();
        return outcome::success();
    }
}
