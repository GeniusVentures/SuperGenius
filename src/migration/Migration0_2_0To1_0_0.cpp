/**
 * @file       Migration0_2_0To1_0_0.cpp
 * @brief      Implementation of MigrationManager and migration steps.
 * @date       2025-05-29
 * @author     Luiz Guilherme Rizzatto Zucchi
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#include "Migration0_2_0To1_0_0.hpp"

#include <filesystem>
#include <boost/format.hpp>
#include <boost/system/error_code.hpp>
#include "account/TransactionManager.hpp"
#include "account/TransferTransaction.hpp"
#include "MigrationManager.hpp"

namespace
{
    std::string BuildLegacyDbPath( const std::string &write_base_path,
                                   const std::string &base58_key,
                                   const std::string &suffix )
    {
        static constexpr auto LEGACY_PREFIX_FMT = "SuperGNUSNode.TestNet.2a.00.%1%";

        const auto legacyNetworkFullPath = ( boost::format( LEGACY_PREFIX_FMT ) % base58_key ).str();
        return ( boost::format( "%s%s_%s" ) % write_base_path % legacyNetworkFullPath % suffix ).str();
    }
} // namespace

namespace sgns
{
    namespace
    {
        std::string BuildLegacyTransactionPath( const GeniusTransaction &tx )
        {
            return tx.GetSrcAddress() + "/tx/" + tx.GetTransactionSpecificPath() + "/" +
                   std::to_string( tx.dag_st.nonce() );
        }

        std::string BuildLegacyProofPath( const GeniusTransaction &tx )
        {
            return tx.GetSrcAddress() + "/proof/" + tx.GetTransactionSpecificPath() + "/" +
                   std::to_string( tx.dag_st.nonce() );
        }
    }

    Migration0_2_0To1_0_0::Migration0_2_0To1_0_0(
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

    std::string Migration0_2_0To1_0_0::FromVersion() const
    {
        return "0.2.0";
    }

    std::string Migration0_2_0To1_0_0::ToVersion() const
    {
        return "1.0.0";
    }

    outcome::result<void> Migration0_2_0To1_0_0::Init()
    {
        auto outDb_result = InitLegacyDb( "out" );
        if ( !outDb_result.has_value() )
        {
            // Clean up target_db on failure
            db_1_0_0_.reset();
            return outDb_result.as_failure();
        }
        db_0_0_2_out_ = std::move( outDb_result.value() );

        auto inDb_result = InitLegacyDb( "in" );
        if ( !inDb_result.has_value() )
        {
            // Clean up previously initialized databases on failure
            db_0_0_2_out_.reset();
            db_1_0_0_.reset();
            return inDb_result.as_failure();
        }
        db_0_0_2_in_ = std::move( inDb_result.value() );

        if ( db_0_0_2_out_ && db_0_0_2_in_ )
        {
            BOOST_OUTCOME_TRY( auto target_db, InitTargetDb() );
            db_1_0_0_ = std::move( target_db );
        }

        return outcome::success();
    }

    outcome::result<bool> Migration0_2_0To1_0_0::IsRequired() const
    {
        if ( !db_0_0_2_out_ || !db_0_0_2_in_ )
        {
            m_logger->debug( "Legacy 0.2.0 DBs not found; skipping Migration0_2_0To1_0_0" );
            return false;
        }

        sgns::crdt::GlobalDB::Buffer version_key;
        version_key.put( std::string( MigrationManager::VERSION_INFO_KEY ) );
        auto version_ret = db_1_0_0_->GetDataStore()->get( version_key );

        if ( version_ret.has_error() )
        {
            m_logger->debug( "Can't find version on db {}, {}", ToVersion(), MigrationManager::VERSION_INFO_KEY );
            return true;
        }

        auto version = version_ret.value();

        if ( !IsVersionLessThan( std::string( version.toString() ), ToVersion() ) )
        {
            m_logger->debug( "newDb is already migrated; skipping Migration0_2_0To1_0_0" );
            return false;
        }
        else
        {
            m_logger->debug( "Not migrated: {}", version.toString() );
        }
        return true;
    }

    outcome::result<std::shared_ptr<crdt::GlobalDB>> Migration0_2_0To1_0_0::InitTargetDb()
    {
        static constexpr auto DATABASE_1_0_0_PREFIX = "SuperGNUSNode.TestNet.2a.01.%1%";

        const auto legacyNetworkFullPath = ( boost::format( DATABASE_1_0_0_PREFIX ) % base58key_ ).str();
        const auto fullPath              = ( boost::format( "%s%s" ) % writeBasePath_ % legacyNetworkFullPath ).str();

        m_logger->debug( "Initializing 1.0.0 DB at path {}", fullPath );

        auto maybe_db_1_0_0 = crdt::GlobalDB::New( ioContext_,
                                                   fullPath,
                                                   pubSub_,
                                                   crdt::CrdtOptions::DefaultOptions(),
                                                   graphsync_,
                                                   scheduler_,
                                                   generator_ );

        if ( !maybe_db_1_0_0.has_value() )
        {
            m_logger->error( "1.0.0 database not created on {}", fullPath );
            return outcome::failure( boost::system::error_code{} );
        }

        m_logger->debug( "Started DB at path {}", fullPath );

        return std::move( maybe_db_1_0_0.value() );
    }

    outcome::result<std::shared_ptr<crdt::GlobalDB>> Migration0_2_0To1_0_0::InitLegacyDb( const std::string &suffix )
    {
        const auto fullPath = BuildLegacyDbPath( writeBasePath_, base58key_, suffix );

        if ( !std::filesystem::exists( fullPath ) )
        {
            m_logger->debug( "Legacy DB not found at {}; skipping initialization", fullPath );
            return std::shared_ptr<crdt::GlobalDB>{};
        }

        m_logger->debug( "Initializing legacy DB at path {}", fullPath );

        BOOST_OUTCOME_TRY( auto db,
                           crdt::GlobalDB::New( ioContext_,
                                                fullPath,
                                                pubSub_,
                                                crdt::CrdtOptions::DefaultOptions(),
                                                graphsync_,
                                                scheduler_,
                                                generator_ ) );
        m_logger->debug( "Started legacy DB at path {}", fullPath );

        return db;
    }

    outcome::result<uint32_t> Migration0_2_0To1_0_0::MigrateDb( const std::shared_ptr<crdt::GlobalDB> &oldDb,
                                                                const std::shared_ptr<crdt::GlobalDB> &newDb )
    {
        // Outgoing transactions were /bc-963/[self address]/tx/[type]/[nonce]
        // Incoming transactions were /bc-963/[other address]/notify/tx/[tx hash]
        // Outgoing proofs were /bc-963/[self address]/proof/[nonce]
        // Incoming proofs were /bc-963/[other address]/notify/proof/[tx hash]

        const std::string BASE                 = "/bc-963/";
        auto              maybeTransactionKeys = oldDb->QueryKeyValues( BASE, "*", "/tx" );
        if ( !maybeTransactionKeys.has_value() )
        {
            m_logger->error( "Failed to query transaction keys with base {}", BASE );
            return outcome::failure( boost::system::error_code{} );
        }

        auto &entries = maybeTransactionKeys.value();
        m_logger->debug( "Found {} transaction keys to migrate", entries.size() );
        size_t migrated_count = 0;
        size_t BATCH_SIZE     = 50;

        for ( const auto &entry : entries )
        {
            auto keyOpt = oldDb->KeyToString( entry.first );
            if ( !keyOpt.has_value() )
            {
                m_logger->error( "Failed to convert key buffer to string" );
                continue;
            }

            std::string transaction_key   = keyOpt.value();
            auto        maybe_transaction = TransactionManager::FetchTransaction( *oldDb, transaction_key );
            if ( !maybe_transaction.has_value() )
            {
                m_logger->error( "Can't fetch transaction for key {}", transaction_key );
                continue;
            }
            auto tx = maybe_transaction.value();
            m_logger->trace( "Fetched transaction {}", transaction_key );

            if ( !tx->CheckDAGSignatureLegacy() )
            {
                m_logger->error( "Could not validate signature of transaction {}", transaction_key );
                continue;
            }

            std::string proof_key         = transaction_key;
            std::string tx_notify_path    = "/notify/tx/";
            std::string proof_notify_path = "/notify/proof/";
            size_t      notify_position   = transaction_key.find( tx_notify_path );
            if ( notify_position != std::string::npos )
            {
                proof_key.replace( notify_position, tx_notify_path.length(), proof_notify_path );

                m_logger->trace( "Searching for notify proof {}", transaction_key );
            }
            else
            {
                proof_key = BASE + tx->GetSrcAddress() + "/proof/" + std::to_string( tx->dag_st.nonce() );
            }

            auto maybe_proof_data = oldDb->Get( proof_key );
            if ( !maybe_proof_data.has_value() )
            {
                m_logger->error( "Can't find the proof data for {}", transaction_key );
                continue;
            }

            auto                        transaction_path = BASE + BuildLegacyTransactionPath( *tx );
            sgns::crdt::HierarchicalKey tx_key( transaction_path );

            auto has_tx     = crdt_transaction_->HasKey( tx_key );
            bool migrate_tx = true;
            if ( has_tx )
            {
                migrate_tx               = false;
                auto maybe_replicated_tx = crdt_transaction_->Get( tx_key );
                if ( maybe_replicated_tx.has_value() )
                {
                    //decide which one to use
                    auto maybe_deserialized_tx = TransactionManager::DeSerializeTransaction(
                        maybe_replicated_tx.value() );
                    if ( maybe_deserialized_tx.has_value() )
                    {
                        auto previous_tx = maybe_transaction.value();
                        if ( previous_tx->dag_st.timestamp() > tx->dag_st.timestamp() )
                        {
                            //need to update, the new one came first
                            migrate_tx = true;
                            m_logger->debug( "Need to remove previous transaction, since new one is older {}",
                                             transaction_path );

                            BOOST_OUTCOME_TRY( crdt_transaction_->Erase( tx_key ) );

                            sgns::crdt::HierarchicalKey replicated_proof_key( BASE + BuildLegacyProofPath( *tx ) );

                            m_logger->debug( "Need to remove previous proof as well {}",
                                             replicated_proof_key.GetKey() );
                            BOOST_OUTCOME_TRY( crdt_transaction_->Erase( replicated_proof_key ) );
                        }
                        else
                        {
                            m_logger->debug( "Currently migrated transaction has earlier timestamp {}",
                                             transaction_path );
                        }
                    }
                    else
                    {
                        migrate_tx = true;
                        m_logger->debug( "Invalid transaction, deleting from migration {}", transaction_path );

                        BOOST_OUTCOME_TRY( crdt_transaction_->Erase( tx_key ) );

                        sgns::crdt::HierarchicalKey replicated_proof_key( BASE + BuildLegacyProofPath( *tx ) );

                        m_logger->debug( "Need to remove previous proof as well {}", replicated_proof_key.GetKey() );
                        BOOST_OUTCOME_TRY( crdt_transaction_->Erase( replicated_proof_key ) );
                    }
                }
            }
            if ( migrate_tx )
            {
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
                BOOST_OUTCOME_TRY( crdt_transaction_->Put( std::move( tx_key ), std::move( data_transaction ) ) );

                sgns::crdt::HierarchicalKey  proof_crdt_key( BASE + BuildLegacyProofPath( *tx ) );
                sgns::crdt::GlobalDB::Buffer proof_transaction;
                proof_transaction.put( maybe_proof_data.value() );
                BOOST_OUTCOME_TRYV2(
                    auto &&,
                    crdt_transaction_->Put( std::move( proof_crdt_key ), std::move( proof_transaction ) ) );
                m_logger->trace( "Proof recorded for transaction {}", transaction_key );
            }
            else
            {
                m_logger->debug( "Not migrating transaction {}", transaction_path );
            }
            ++migrated_count;
            if ( migrated_count >= BATCH_SIZE )
            {
                BOOST_OUTCOME_TRY( crdt_transaction_->Commit( topics_ ) );
                crdt_transaction_ = db_1_0_0_->BeginTransaction(); // start fresh
                topics_.clear();

                topics_.emplace( std::string( TransactionManager::GNUS_FULL_NODES_TOPIC ) );
                migrated_count = 0;
                m_logger->debug( "Committed a batch of {} transactions", BATCH_SIZE );
            }
        }

        m_logger->trace( "Successfully committed all migrated entries to new DB" );
        return migrated_count;
    }

    outcome::result<void> Migration0_2_0To1_0_0::Apply()
    {
        if ( !db_0_0_2_out_ || !db_0_0_2_in_ )
        {
            m_logger->error( "Legacy DBs were not initialized; nothing to migrate for Migration0_2_0To1_0_0" );
            return outcome::success();
        }

        m_logger->debug( "Starting Apply step of Migration0_2_0To1_0_0" );

        crdt_transaction_ = db_1_0_0_->BeginTransaction();
        topics_.clear();

        topics_.emplace( std::string( TransactionManager::GNUS_FULL_NODES_TOPIC ) );

        m_logger->debug( "Migrating output DB into new DB" );
        BOOST_OUTCOME_TRY( auto remainder_outdb, MigrateDb( db_0_0_2_out_, db_1_0_0_ ) );

        if ( remainder_outdb > 0 )
        {
            for ( auto &topic : topics_ )
            {
                m_logger->debug( "Commiting migrating to topics {}", topic );
            }
            BOOST_OUTCOME_TRY( crdt_transaction_->Commit( topics_ ) );
            crdt_transaction_ = db_1_0_0_->BeginTransaction();
            topics_.clear();
            topics_.emplace( std::string( TransactionManager::GNUS_FULL_NODES_TOPIC ) );
            m_logger->debug( "Committed remainder of output transactions: {}", remainder_outdb );
        }

        m_logger->debug( "Migrating input DB into new DB" );
        BOOST_OUTCOME_TRY( auto remainder_indb, MigrateDb( db_0_0_2_in_, db_1_0_0_ ) );

        if ( remainder_indb > 0 )
        {
            for ( auto &topic : topics_ )
            {
                m_logger->debug( "Commiting migrating to topics {}", topic );
            }
            BOOST_OUTCOME_TRY( crdt_transaction_->Commit( topics_ ) );
        }
        sgns::crdt::GlobalDB::Buffer version_buffer;
        sgns::crdt::GlobalDB::Buffer version_key;
        version_key.put( std::string( MigrationManager::VERSION_INFO_KEY ) );
        version_buffer.put( ToVersion() );

        BOOST_OUTCOME_TRY( db_1_0_0_->GetDataStore()->put( version_key, version_buffer ) );

        m_logger->debug( "Apply step of Migration0_2_0To1_0_0 finished successfully" );

        return outcome::success();
    }

    outcome::result<void> Migration0_2_0To1_0_0::ShutDown()
    {
        db_0_0_2_in_.reset();
        db_0_0_2_out_.reset();
        crdt_transaction_.reset();
        db_1_0_0_.reset();
        return outcome::success();
    }

} // namespace sgns
