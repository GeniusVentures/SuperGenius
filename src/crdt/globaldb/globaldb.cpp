#include "globaldb.hpp"
#include "pubsub_broadcaster_ext.hpp"
#include "keypair_file_storage.hpp"

#include "crdt/crdt_datastore.hpp"
#include "crdt/graphsync_dagsyncer.hpp"
#include "crdt/atomic_transaction.hpp"
#include "storage/database_error.hpp"

#include <ipfs_lite/ipfs/impl/datastore_rocksdb.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/graphsync_impl.hpp>

#include <rocksdb/db.h>
#include <rocksdb/utilities/backup_engine.h>

#include <libp2p/host/host.hpp>
#include <libp2p/injector/host_injector.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/injector/kademlia_injector.hpp>
#include <boost/format.hpp>
#include <chrono>
#include <thread>

#if defined( _WIN32 )
#include <winsock2.h>
#include <iphlpapi.h>
#pragma comment( lib, "iphlpapi.lib" )
#pragma comment( lib, "ws2_32.lib" )
#else
#endif

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns::crdt, GlobalDB::Error, e )
{
    using ProofError = sgns::crdt::GlobalDB::Error;
    switch ( e )
    {
        case ProofError::ROCKSDB_IO:
            return "RocksDB I/O error";
        case ProofError::IPFS_DB_NOT_CREATED:
            return "IPFS Database creation error";
        case ProofError::DAG_SYNCHER_NOT_LISTENING:
            return "DAG Syncher listen error";
        case ProofError::CRDT_DATASTORE_NOT_CREATED:
            return "CRDT DataStore creation error";
        case ProofError::PUBSUB_BROADCASTER_NOT_CREATED:
            return "Pubsub Broadcaster creation error";
        case ProofError::INVALID_PARAMETERS:
            return "Invalid parameters provided";
        case ProofError::GLOBALDB_NOT_STARTED:
            return "Start method wasn't called";
        case ProofError::POST_COMMIT_ERASE_ANOMALY:
            return "Erase committed, but the post-commit seal re-check failed (data was erased)";
    }
    return "Unknown error";
}

namespace sgns::crdt
{

    using CrdtOptions        = crdt::CrdtOptions;
    using CrdtDatastore      = crdt::CrdtDatastore;
    using HierarchicalKey    = crdt::HierarchicalKey;
    using GraphsyncDAGSyncer = crdt::GraphsyncDAGSyncer;
    using RocksdbDatastore   = ipfs_lite::ipfs::RocksdbDatastore;
    using IpfsRocksDb        = ipfs_lite::rocksdb;
    using GossipPubSub       = ipfs_pubsub::GossipPubSub;
    using GraphsyncImpl      = ipfs_lite::ipfs::graphsync::GraphsyncImpl;

    outcome::result<std::shared_ptr<GlobalDB>> GlobalDB::New(
        std::shared_ptr<boost::asio::io_context>                              context,
        std::string                                                           databasePath,
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub>                      pubsub,
        std::shared_ptr<CrdtOptions>                                          crdtOptions,
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Network>            graphsyncnetwork,
        std::shared_ptr<libp2p::basic::Scheduler>                             scheduler,
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
        std::shared_ptr<RocksDB>                                              datastore,
        BackupOptions                                                         backup_options )
    {
        if ( ( !context ) || ( !generator ) || ( !pubsub ) || ( !graphsyncnetwork ) )
        {
            return outcome::failure( Error::INVALID_PARAMETERS );
        }
        auto new_instance = std::shared_ptr<GlobalDB>(
            new GlobalDB( std::move( context ), std::move( databasePath ), std::move( pubsub ) ) );
        new_instance->backup_options_ = backup_options;

        BOOST_OUTCOME_TRY( new_instance->Init( std::move( crdtOptions ),
                                               std::move( graphsyncnetwork ),
                                               std::move( scheduler ),
                                               std::move( generator ),
                                               std::move( datastore ) ) );
        return new_instance;
    }

    GlobalDB::GlobalDB( std::shared_ptr<boost::asio::io_context>         context,
                        std::string                                      databasePath,
                        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub ) :
        m_context( std::move( context ) ),
        m_databasePath( std::move( databasePath ) ),
        m_pubsub( std::move( pubsub ) ),
        started_{ false },
        cid_sync_started_{ false },
        cid_receiving_started_{ false },
        head_broadcasting_started_{ false }
    {
    }

    GlobalDB::~GlobalDB()
    {
        m_logger->debug( "~GlobalDB CALLED with count {} on {} ", m_datastore.use_count(), m_databasePath );
        ShutdownNow();
    }

    void GlobalDB::ShutdownNow()
    {
        bool expected = false;
        if ( !shutdown_started_.compare_exchange_strong( expected, true ) )
        {
            return;
        }

        m_logger->info( "GlobalDB shutdown start" );

        //SetIncomingBroadcastEnabled( false );
        StopBackupLoop();

        std::shared_ptr<PubSubBroadcasterExt> broadcaster;
        std::shared_ptr<CrdtDatastore>        crdt_datastore;
        {
            std::lock_guard<std::mutex> lock( lifecycle_mutex_ );
            broadcaster    = std::move( m_broadcaster );
            crdt_datastore = std::move( m_crdtDatastore );
            m_datastore.reset();
        }

        if ( broadcaster )
        {
            broadcaster->Stop();
        }

        if ( crdt_datastore )
        {
            crdt_datastore->CancelAndCloseNow();
        }

        started_.store( false );
        m_logger->info( "GlobalDB shutdown finished" );
    }

    std::shared_ptr<GlobalDB::RocksDB> GlobalDB::ActiveDataStore() const
    {
        std::lock_guard<std::mutex> lock( lifecycle_mutex_ );
        if ( shutdown_started_.load() )
        {
            return nullptr;
        }
        return m_datastore;
    }

    std::shared_ptr<CrdtDatastore> GlobalDB::ActiveCRDTDataStore() const
    {
        std::lock_guard<std::mutex> lock( lifecycle_mutex_ );
        if ( shutdown_started_.load() )
        {
            return nullptr;
        }
        return m_crdtDatastore;
    }

    std::shared_ptr<PubSubBroadcasterExt> GlobalDB::ActiveBroadcaster() const
    {
        std::lock_guard<std::mutex> lock( lifecycle_mutex_ );
        if ( shutdown_started_.load() )
        {
            return nullptr;
        }
        return m_broadcaster;
    }

    outcome::result<void> GlobalDB::Init(
        std::shared_ptr<CrdtOptions>                                          crdtOptions,
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Network>            graphsyncnetwork,
        std::shared_ptr<libp2p::basic::Scheduler>                             scheduler,
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
        std::shared_ptr<RocksDB>                                              datastore )
    {
        std::shared_ptr<RocksDB> dataStore = std::move( datastore );
        if ( dataStore == nullptr )
        {
            auto databasePathAbsolute = boost::filesystem::absolute( m_databasePath ).string();
            backup_directory_         = ResolveBackupDirectory( databasePathAbsolute );

            // Create new database
            m_logger->info( "Opening database " + databasePathAbsolute );
            RocksDB::Options options;
            options.create_if_missing                    = true; // intentionally
            options.target_file_size_base                = 32 * 1024 * 1024;
            options.max_compaction_bytes                 = 32 * 1024 * 1024;
            options.write_buffer_size                    = 32 * 1024 * 1024;
            options.level0_file_num_compaction_trigger   = 1;
            options.target_file_size_multiplier          = 1;
            options.level_compaction_dynamic_level_bytes = false;
            try
            {
                constexpr int  kMaxLockRetries = 30;
                constexpr auto kRetrySleep     = std::chrono::milliseconds( 100 );

                auto is_retryable_open_error = []( storage::DatabaseError error )
                {
                    return error == storage::DatabaseError::IO_ERROR || error == storage::DatabaseError::BUSY ||
                           error == storage::DatabaseError::TRY_AGAIN_ || error == storage::DatabaseError::TIMED_OUT ||
                           error == storage::DatabaseError::SHUTDOWN_IN_PROGRESS;
                };

                int  lock_retry_count = 0;
                auto dataStoreResult  = RocksDB::create( databasePathAbsolute, options );

                // One-shot restore-from-backup on corruption — must not retry inside the lock loop.
                if ( !dataStoreResult.has_value() )
                {
                    std::string errorMsg = dataStoreResult.error().message();
                    if ( errorMsg.find( "corruption" ) != std::string::npos ||
                         errorMsg.find( "Corruption" ) != std::string::npos )
                    {
                        m_logger->warn( "Database corruption detected, attempting restore from latest backup: {}",
                                        databasePathAbsolute );

                        auto restoreFromLatestBackup = [&]() -> bool
                        {
                            if ( !( backup_options_.enabled && backup_options_.auto_restore_on_repair_failure ) )
                            {
                                m_logger->error(
                                    "Backup restore is disabled; cannot recover corrupted DB from backup" );
                                return false;
                            }

                            ::ROCKSDB_NAMESPACE::BackupEngineReadOnly *backup_engine = nullptr;
                            ::ROCKSDB_NAMESPACE::BackupEngineOptions   backup_options_engine( backup_directory_ );
                            auto open_backup_status = ::ROCKSDB_NAMESPACE::BackupEngineReadOnly::Open(
                                ::ROCKSDB_NAMESPACE::Env::Default(),
                                backup_options_engine,
                                &backup_engine );

                            if ( !open_backup_status.ok() || backup_engine == nullptr )
                            {
                                m_logger->error( "Could not open backup engine at {}: {}",
                                                 backup_directory_,
                                                 open_backup_status.ToString() );
                                return false;
                            }

                            std::unique_ptr<::ROCKSDB_NAMESPACE::BackupEngineReadOnly> backup_guard( backup_engine );

                            ::ROCKSDB_NAMESPACE::RestoreOptions restore_options;
                            restore_options.keep_log_files = false;

                            auto restore_status = backup_guard->RestoreDBFromLatestBackup( databasePathAbsolute,
                                                                                           databasePathAbsolute,
                                                                                           restore_options );
                            if ( !restore_status.ok() )
                            {
                                m_logger->error( "Restore from latest backup failed: {}", restore_status.ToString() );
                                return false;
                            }

                            m_logger->warn( "Database restored from latest backup, retrying open" );
                            dataStoreResult = RocksDB::create( databasePathAbsolute, options );
                            return dataStoreResult.has_value();
                        };

                        if ( !restoreFromLatestBackup() )
                        {
                            m_logger->critical( "Restore from backup failed for corrupted DB {}. "
                                                "Terminating process to preserve on-disk state for forensic analysis.",
                                                databasePathAbsolute );
                            std::abort();
                        }
                    }
                }

                // Lock-retry loop: only for transient / retryable errors (lock, busy, etc.)
                while ( !dataStoreResult.has_value() )
                {
                    auto errorCode = static_cast<storage::DatabaseError>( dataStoreResult.error().value() );

                    if ( is_retryable_open_error( errorCode ) && lock_retry_count < kMaxLockRetries )
                    {
                        ++lock_retry_count;
                        m_logger->warn( "Database {} not open yet ({}). Retrying open in {}ms ({}/{})",
                                        databasePathAbsolute,
                                        dataStoreResult.error().message(),
                                        kRetrySleep.count(),
                                        lock_retry_count,
                                        kMaxLockRetries );
                        std::this_thread::sleep_for( kRetrySleep );
                        dataStoreResult = RocksDB::create( databasePathAbsolute, options );
                        continue;
                    }

                    m_logger->error( "Unable to open database {}: {}",
                                     databasePathAbsolute,
                                     dataStoreResult.error().message() );
                    return dataStoreResult.error();
                }

                if ( lock_retry_count > 0 )
                {
                    m_logger->info( "Opened database {} after {} open retries",
                                    databasePathAbsolute,
                                    lock_retry_count );
                }

                dataStore = std::move( dataStoreResult.value() );
            }
            catch ( std::exception &e )
            {
                m_logger->error( "Unable to open database: " + std::string( e.what() ) );
                return Error::ROCKSDB_IO;
            }
        }
        m_datastore = std::move( dataStore );

        auto ipfsDBResult = IpfsRocksDb::create( m_datastore->getDB() );
        if ( ipfsDBResult.has_error() )
        {
            m_logger->error( "Unable to create database for IPFS datastore" );
            return Error::IPFS_DB_NOT_CREATED;
        }

        auto ipfsDataStore = std::make_shared<RocksdbDatastore>( ipfsDBResult.value() );

        if ( !m_pubsub )
        {
            m_logger->error( "pubsub not initialized." );
            return outcome::failure( Error::DAG_SYNCHER_NOT_LISTENING );
        }
        std::shared_ptr<libp2p::Host> host = m_pubsub->GetHost();

        // Run Graphsync response handling on the executor that owns the pubsub host's stream queues.
        auto graphsync = std::make_shared<GraphsyncImpl>( host,
                                                          std::move( scheduler ),
                                                          graphsyncnetwork,
                                                          generator,
                                                          m_pubsub->GetAsioContext() );
        auto dagSyncer = std::make_shared<GraphsyncDAGSyncer>( ipfsDataStore, graphsync, host );
        m_dagSyncer    = dagSyncer;

        // Start DagSyner listener
        auto startResult = dagSyncer->StartSync();
        if ( startResult.has_failure() )
        {
            m_logger->error( "DAG Syncher not listening" );
            return startResult.error();
        }

        m_broadcaster = PubSubBroadcasterExt::New( dagSyncer, m_pubsub );
        if ( m_broadcaster == nullptr )
        {
            m_logger->error( "Unable to create PubSub broadcaster" );
            return Error::PUBSUB_BROADCASTER_NOT_CREATED;
        }
        m_crdtDatastore = CrdtDatastore::New( m_datastore,
                                              HierarchicalKey( "crdt" ),
                                              dagSyncer,
                                              m_broadcaster,
                                              crdtOptions );
        if ( m_crdtDatastore == nullptr )
        {
            m_logger->error( "Unable to create CRDT datastore" );
            return Error::CRDT_DATASTORE_NOT_CREATED;
        }

        return outcome::success();
    }

    std::string GlobalDB::ResolveBackupDirectory( const std::string &databasePathAbsolute ) const
    {
        return ( std::filesystem::path( databasePathAbsolute ) / "backups" ).string();
    }

    void GlobalDB::CreateBackupNow()
    {
        if ( !backup_options_.enabled || !m_datastore )
        {
            return;
        }

        if ( backup_directory_.empty() )
        {
            auto databasePathAbsolute = boost::filesystem::absolute( m_databasePath ).string();
            backup_directory_         = ResolveBackupDirectory( databasePathAbsolute );
        }

        std::error_code fs_error;
        std::filesystem::create_directories( backup_directory_, fs_error );
        if ( fs_error )
        {
            m_logger->error( "Failed to create backup directory {}: {}", backup_directory_, fs_error.message() );
            return;
        }

        ::ROCKSDB_NAMESPACE::BackupEngine       *backup_engine = nullptr;
        ::ROCKSDB_NAMESPACE::BackupEngineOptions backup_options_engine( backup_directory_ );
        auto open_status = ::ROCKSDB_NAMESPACE::BackupEngine::Open( ::ROCKSDB_NAMESPACE::Env::Default(),
                                                                    backup_options_engine,
                                                                    &backup_engine );
        if ( !open_status.ok() || backup_engine == nullptr )
        {
            m_logger->error( "Failed to open backup engine at {}: {}", backup_directory_, open_status.ToString() );
            return;
        }

        std::unique_ptr<::ROCKSDB_NAMESPACE::BackupEngine> backup_guard( backup_engine );

        auto create_status = backup_guard->CreateNewBackup( m_datastore->getDB().get(), true );
        if ( !create_status.ok() )
        {
            m_logger->error( "CreateNewBackup failed: {}", create_status.ToString() );
            return;
        }

        auto purge_status = backup_guard->PurgeOldBackups( backup_options_.keep_count );
        if ( !purge_status.ok() )
        {
            m_logger->warn( "PurgeOldBackups failed: {}", purge_status.ToString() );
        }

        m_logger->info( "Backup created successfully in {}", backup_directory_ );
    }

    void GlobalDB::StartBackupLoop()
    {
        if ( !backup_options_.enabled || backup_thread_.joinable() || !m_pubsub )
        {
            return;
        }

        if ( backup_options_.interval_minutes == 0 )
        {
            backup_options_.interval_minutes = 15;
        }
        if ( backup_options_.keep_count == 0 )
        {
            backup_options_.keep_count = 12;
        }

        stop_backup_thread_.store( false );
        backup_thread_ = std::thread(
            [this]()
            {
                CreateBackupNow();

                while ( !stop_backup_thread_.load() )
                {
                    std::unique_lock<std::mutex> lock( backup_wait_mutex_ );
                    const bool                   stop_requested = backup_wait_cv_.wait_for(
                        lock,
                        std::chrono::minutes( backup_options_.interval_minutes ),
                        [this]() { return stop_backup_thread_.load(); } );
                    lock.unlock();

                    if ( stop_requested )
                    {
                        break;
                    }
                    CreateBackupNow();
                }
            } );
    }

    void GlobalDB::StopBackupLoop()
    {
        stop_backup_thread_.store( true );
        backup_wait_cv_.notify_all();
        if ( backup_thread_.joinable() )
        {
            backup_thread_.join();
        }
    }

    void GlobalDB::Start()
    {
        if ( started_ )
        {
            return;
        }
        StartCIDReceiving();
        StartCICSync();
        StartRebroadcastHeads();
        started_ = true;
        StartBackupLoop();
    }

    void GlobalDB::StartCIDReceiving()
    {
        if ( cid_receiving_started_ )
        {
            return;
        }
        auto broadcaster = ActiveBroadcaster();
        if ( !broadcaster )
        {
            return;
        }
        broadcaster->Start();
        cid_receiving_started_ = true;
    }

    void GlobalDB::StartCICSync()
    {
        if ( cid_sync_started_ )
        {
            return;
        }
        auto crdt_datastore = ActiveCRDTDataStore();
        if ( !crdt_datastore )
        {
            return;
        }
        crdt_datastore->StartCIDProcessing();
        cid_sync_started_ = true;
    }

    void GlobalDB::StartRebroadcastHeads()
    {
        if ( head_broadcasting_started_ )
        {
            return;
        }
        auto crdt_datastore = ActiveCRDTDataStore();
        if ( !crdt_datastore )
        {
            return;
        }
        crdt_datastore->StartRebroadcastHeads();
        head_broadcasting_started_ = true;
    }

    outcome::result<CID> GlobalDB::Put( const HierarchicalKey                 &key,
                                        const Buffer                          &value,
                                        const std::unordered_set<std::string> &topics )
    {
        auto crdt_datastore = ActiveCRDTDataStore();
        if ( !crdt_datastore )
        {
            return outcome::failure( std::errc::operation_canceled );
        }
        return crdt_datastore->PutKey( key, value, topics );
    }

    outcome::result<CID> GlobalDB::PutConvergentImmutable( const HierarchicalKey                 &key,
                                                            const Buffer                          &value,
                                                            const std::unordered_set<std::string> &topics )
    {
        // ShutdownNow() moves the datastore handle out: dereferencing the raw
        // member here is the exact class of null-deref that segfaulted
        // migration_sync_test on aarch64 (see the ActiveDataStore wrappers).
        auto crdt_datastore = ActiveCRDTDataStore();
        if ( !crdt_datastore )
        {
            return outcome::failure( std::errc::operation_canceled );
        }
        return crdt_datastore->PutConvergentImmutableKey( key, value, topics );
    }

    outcome::result<CID> GlobalDB::Put( const std::vector<DataPair>           &data_vector,
                                        const std::unordered_set<std::string> &topics )
    {
        auto crdt_datastore = ActiveCRDTDataStore();
        if ( !crdt_datastore )
        {
            return outcome::failure( std::errc::operation_canceled );
        }
        AtomicTransaction batch( std::move( crdt_datastore ) );

        for ( auto &data : data_vector )
        {
            BOOST_OUTCOME_TRY( batch.Put( std::get<0>( data ), std::get<1>( data ) ) );
        }

        return batch.Commit( topics );
    }

    outcome::result<void> GlobalDB::PutLocal( const HierarchicalKey &key, const Buffer &value, const std::string &id )
    {
        return m_crdtDatastore->PutKeyLocal( key, value, id );
    }

    outcome::result<GlobalDB::Buffer> GlobalDB::Get( const HierarchicalKey &key )
    {
        auto crdt_datastore = ActiveCRDTDataStore();
        if ( !crdt_datastore )
        {
            return outcome::failure( std::errc::operation_canceled );
        }
        return crdt_datastore->GetKey( key );
    }

    outcome::result<CID> GlobalDB::Remove( const HierarchicalKey &key, const std::unordered_set<std::string> &topics )
    {
        auto crdt_datastore = ActiveCRDTDataStore();
        if ( !crdt_datastore )
        {
            return outcome::failure( std::errc::operation_canceled );
        }
        return crdt_datastore->DeleteKey( key, topics );
    }

    outcome::result<GlobalDB::QueryResult> GlobalDB::QueryKeyValues( std::string_view keyPrefix )
    {
        auto crdt_datastore = ActiveCRDTDataStore();
        if ( !crdt_datastore )
        {
            return outcome::failure( std::errc::operation_canceled );
        }
        return crdt_datastore->QueryKeyValues( keyPrefix );
    }

    outcome::result<GlobalDB::QueryResult> GlobalDB::QueryKeyValues( const std::string &prefix_base,
                                                                     const std::string &middle_part,
                                                                     const std::string &remainder_prefix )
    {
        auto crdt_datastore = ActiveCRDTDataStore();
        if ( !crdt_datastore )
        {
            return outcome::failure( std::errc::operation_canceled );
        }
        return crdt_datastore->QueryKeyValues( prefix_base, middle_part, remainder_prefix );
    }

    outcome::result<std::string> GlobalDB::KeyToString( const Buffer &key ) const
    {
        // @todo cache the prefix and suffix
        auto crdt_datastore = ActiveCRDTDataStore();
        if ( !crdt_datastore )
        {
            return outcome::failure( std::errc::operation_canceled );
        }
        auto keysPrefix  = crdt_datastore->GetKeysPrefix();
        auto valueSuffix = crdt_datastore->GetValueSuffix();

        auto sKey = std::string( key.toString() );

        if ( auto prefixPos = keysPrefix.empty() ? 0 : sKey.find( keysPrefix, 0 ); prefixPos != 0 )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        size_t keyPos = keysPrefix.size();

        auto suffixPos = valueSuffix.empty() ? sKey.size() : sKey.rfind( valueSuffix, std::string::npos );
        if ( suffixPos == std::string::npos || suffixPos < keyPos )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        return sKey.substr( keyPos, suffixPos - keyPos );
    }

    void GlobalDB::PrintDataStore()
    {
        if ( auto crdt_datastore = ActiveCRDTDataStore() )
        {
            crdt_datastore->PrintDataStore();
        }
    }

    std::shared_ptr<AtomicTransaction> GlobalDB::BeginTransaction()
    {
        auto crdt_datastore = ActiveCRDTDataStore();
        return crdt_datastore ? std::make_shared<AtomicTransaction>( std::move( crdt_datastore ) ) : nullptr;
    }

    outcome::result<void> GlobalDB::AddBroadcastTopic( const std::string &topicName )
    {
        auto broadcaster = ActiveBroadcaster();
        if ( !broadcaster )
        {
            return outcome::failure( std::errc::operation_canceled );
        }
        return broadcaster->AddBroadcastTopic( topicName );
    }

    void GlobalDB::AddTopicName( std::string topicName )
    {
        if ( auto crdt_datastore = ActiveCRDTDataStore() )
        {
            crdt_datastore->AddTopicName( topicName );
        }
    }

    void GlobalDB::AddListenTopic( std::string topicName )
    {
        auto broadcaster    = ActiveBroadcaster();
        auto crdt_datastore = ActiveCRDTDataStore();
        if ( broadcaster && crdt_datastore )
        {
            broadcaster->AddListenTopic( topicName );
            crdt_datastore->AddTopicName( std::move(topicName) );
        }
    }

    bool GlobalDB::RegisterElementFilter( const std::string &pattern, GlobalDBFilterCallback filter )
    {
        auto crdt_datastore = ActiveCRDTDataStore();
        return crdt_datastore && crdt_datastore->RegisterElementFilter( pattern, std::move( filter ) );
    }

    bool GlobalDB::RegisterNewElementCallback( const std::string &pattern, GlobalDBNewElementCallback callback )
    {
        auto crdt_datastore = ActiveCRDTDataStore();
        return crdt_datastore && crdt_datastore->RegisterNewElementCallback( pattern, std::move( callback ) );
    }

    bool GlobalDB::RegisterDeletedElementCallback( const std::string &pattern, GlobalDBDeletedElementCallback callback )
    {
        auto crdt_datastore = ActiveCRDTDataStore();
        return crdt_datastore && crdt_datastore->RegisterDeletedElementCallback( pattern, std::move( callback ) );
    }

    void GlobalDB::UnregisterElementFilter( const std::string &pattern )
    {
        auto crdt_datastore = ActiveCRDTDataStore();
        if ( crdt_datastore )
        {
            crdt_datastore->UnregisterElementFilter( pattern );
        }
    }

    void GlobalDB::UnregisterNewElementCallback( const std::string &pattern )
    {
        auto crdt_datastore = ActiveCRDTDataStore();
        if ( crdt_datastore )
        {
            crdt_datastore->UnregisterNewElementCallback( pattern );
        }
    }

    void GlobalDB::UnregisterDeletedElementCallback( const std::string &pattern )
    {
        auto crdt_datastore = ActiveCRDTDataStore();
        if ( crdt_datastore )
        {
            crdt_datastore->UnregisterDeletedElementCallback( pattern );
        }
    }

    std::shared_ptr<GlobalDB::RocksDB> GlobalDB::GetDataStore()
    {
        std::lock_guard<std::mutex> lock( lifecycle_mutex_ );
        return m_datastore;
    }

    // ShutdownNow() releases the datastore while CRDT callbacks may still be running, so every
    // caller of GetDataStore() had to null-check the pointer before dereferencing it, and one
    // that did not segfaulted migration_sync_test on aarch64. These three wrappers cover every
    // raw-store operation the callers actually perform, so the check lives here once -- the
    // discipline ActiveCRDTDataStore() already establishes for the other handle.
    outcome::result<GlobalDB::Buffer> GlobalDB::GetRaw( const Buffer &key ) const
    {
        auto datastore = ActiveDataStore();
        if ( !datastore )
        {
            return outcome::failure( std::errc::operation_canceled );
        }
        return datastore->get( key );
    }

    outcome::result<void> GlobalDB::PutRaw( const Buffer &key, const Buffer &value )
    {
        auto datastore = ActiveDataStore();
        if ( !datastore )
        {
            return outcome::failure( std::errc::operation_canceled );
        }
        return datastore->put( key, value );
    }

    outcome::result<GlobalDB::QueryResult> GlobalDB::QueryRaw( const Buffer &key_prefix ) const
    {
        auto datastore = ActiveDataStore();
        if ( !datastore )
        {
            return outcome::failure( std::errc::operation_canceled );
        }
        return datastore->query( key_prefix );
    }

    std::function<bool( int )> GlobalDB::erase_interrupt_hook_for_test_ = nullptr;

    void GlobalDB::SetEraseInterruptHookForTest( std::function<bool( int step )> hook )
    {
        erase_interrupt_hook_for_test_ = std::move( hook );
    }

    outcome::result<void> GlobalDB::EraseEntry( const std::string                              &key,
                                                const CID                                      &cid,
                                                const std::optional<std::pair<Buffer, Buffer>> &extra_entry )
    {
        auto crdt_datastore = ActiveCRDTDataStore();
        auto datastore      = ActiveDataStore();
        if ( !crdt_datastore || !datastore || !m_dagSyncer )
        {
            m_logger->error( "{}: GlobalDB not started", __func__ );
            return outcome::failure( Error::GLOBALDB_NOT_STARTED );
        }

        // D-01: the owner-vended compose critical section, acquired as the
        // compose's FIRST act and held across guard evaluation, staging,
        // the single commit, and the post-commit seal re-assert -- the
        // PutElems discipline (crdt_set.cpp holds mutex_ across
        // build+commit). StageEraseElement's internal lock is reentrant
        // under this hold. Scoped to this one per-tx compose only; never
        // held across evaluations or multiple composes (bounded holds).
        auto compose_lock = crdt_datastore->MakeComposeLock();
        if ( !compose_lock.owns_lock() )
        {
            m_logger->error( "{}: CRDT set not initialized, refusing erase", __func__ );
            return outcome::failure( Error::GLOBALDB_NOT_STARTED );
        }

        // Guards (fail-closed, before any staging): a CID that is in use
        // anywhere in the live DAG machinery is not erased. Guard
        // evaluation runs INSIDE the compose critical section, so a
        // concurrent PutElems cannot interleave between these checks and
        // the commit (WR-02 closed by construction; see the header
        // contract).
        // (1) Head guard: scan the heads of EVERY topic -- a single-topic
        //     check would miss the cid being another topic's head.
        BOOST_OUTCOME_TRY( auto head_list, GetCRDTHeadList() );
        for ( const auto &[topic, topic_heads] : head_list.first )
        {
            if ( topic_heads.count( cid ) > 0 )
            {
                m_logger->error( "{}: cid {} is the head of topic {}, refusing erase",
                                 __func__,
                                 cid.toString().value(),
                                 topic );
                return outcome::failure( Error::INVALID_PARAMETERS );
            }
        }
        // (2) Pending/active root sync job on this CID.
        if ( crdt_datastore->IsRootCIDPendingOrActive( cid ) )
        {
            m_logger->error( "{}: cid {} has a pending or active root job, refusing erase",
                             __func__,
                             cid.toString().value() );
            return outcome::failure( Error::INVALID_PARAMETERS );
        }
        // (3) LRU-cached block.
        if ( m_dagSyncer->IsCIDInCache( cid ) )
        {
            m_logger->error( "{}: cid {} is in the block cache, refusing erase", __func__, cid.toString().value() );
            return outcome::failure( Error::INVALID_PARAMETERS );
        }
        // (4) The seal marker must exist before erasing (unresolved CID means
        //     nothing certifies this node ever completed the block).
        BOOST_OUTCOME_TRY( auto resolved, m_dagSyncer->isResolved( cid ) );
        if ( !resolved )
        {
            m_logger->error( "{}: cid {} is unresolved, refusing erase", __func__, cid.toString().value() );
            return outcome::failure( Error::INVALID_PARAMETERS );
        }

        // Compose: one batch, three owner stagings, one commit. Key-layout
        // knowledge stays in the owners -- this facade only composes.
        auto batch = datastore->batch();
        BOOST_OUTCOME_TRY( crdt_datastore->StageEraseElement( batch, key ) );
        if ( erase_interrupt_hook_for_test_ && erase_interrupt_hook_for_test_( 1 ) )
        {
            return outcome::failure( Error::INVALID_PARAMETERS );
        }
        BOOST_OUTCOME_TRY( m_dagSyncer->StageBlockRemoval( cid, batch ) );
        if ( erase_interrupt_hook_for_test_ && erase_interrupt_hook_for_test_( 2 ) )
        {
            return outcome::failure( Error::INVALID_PARAMETERS );
        }
        BOOST_OUTCOME_TRY( batch->remove( Buffer{}.put( key ) ) );
        if ( erase_interrupt_hook_for_test_ && erase_interrupt_hook_for_test_( 3 ) )
        {
            return outcome::failure( Error::INVALID_PARAMETERS );
        }
        // The caller-built extra entry (D-C1: the /prune/tx/<hash> marker)
        // joins the SAME batch after the raw-map removal, before the single
        // commit -- value XOR marker is established atomically. The buffers
        // are opaque; key shapes stay with the caller (Phase-16 D-01).
        if ( extra_entry.has_value() )
        {
            BOOST_OUTCOME_TRY( batch->put( extra_entry->first, extra_entry->second ) );
        }
        BOOST_OUTCOME_TRY( batch->commit() );

        // The seal marker is never staged for removal above; if it is gone
        // now, something erased it out-of-band -- fail loudly with a
        // DISTINCT error: unlike the pre-staging guard rejections
        // (Error::INVALID_PARAMETERS -- nothing erased), the commit has
        // ALREADY landed here, so callers can tell "erased, then anomaly"
        // apart from "nothing erased".
        BOOST_OUTCOME_TRY( auto still_resolved, m_dagSyncer->isResolved( cid ) );
        if ( !still_resolved )
        {
            m_logger->error( "{}: seal marker vanished after erase - seal-addressing bug (erase already committed)",
                             __func__ );
            return outcome::failure( Error::POST_COMMIT_ERASE_ANOMALY );
        }

        return outcome::success();
    }

    outcome::result<void> GlobalDB::CompactStorage( const std::atomic<bool> *canceled )
    {
        auto datastore = ActiveDataStore();
        if ( !datastore )
        {
            m_logger->error( "{}: GlobalDB not started", __func__ );
            return outcome::failure( Error::GLOBALDB_NOT_STARTED );
        }

        // The ONE home of the whole-DB reclamation recipe (D-C1). The
        // bottommost force flag is load-bearing: the engine default
        // rewrites the bottommost level only when a compaction filter is
        // installed, and none is -- without the force flag the rewrite
        // never happens and deletes survive as un-reclaimed tombstones.
        // Writers are never stalled (the call sleeps until load is low),
        // and exclusive_manual_compaction stays false (the vendored engine
        // warns it does not combine well with the canceled pointer).
        ::ROCKSDB_NAMESPACE::CompactRangeOptions options;
        options.bottommost_level_compaction = ::ROCKSDB_NAMESPACE::BottommostLevelCompaction::kForce;
        options.allow_write_stall           = false;
        // The vendored option is a non-const pointer while the public
        // contract keeps the caller-owned atomic const (never written
        // here); bridging the two is exactly this cast.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        options.canceled = const_cast<std::atomic<bool> *>( canceled );

        // Whole-DB overload: nullptr bounds compact the entire database.
        const auto status = datastore->getDB()->CompactRange( options, nullptr, nullptr );
        if ( status.ok() )
        {
            return outcome::success();
        }
        if ( status.IsManualCompactionPaused() )
        {
            // Pre-set cancellation: nothing was started -- the distinct
            // clean-skip signal (callers log once, never as an error).
            return outcome::failure( std::errc::operation_canceled );
        }
        m_logger->warn( "{}: CompactRange failed: {}", __func__, status.ToString() );
        return outcome::failure( Error::ROCKSDB_IO );
    }

    outcome::result<uint64_t> GlobalDB::GetApproximateDiskUsageBytes() const
    {
        auto datastore = ActiveDataStore();
        if ( !datastore )
        {
            m_logger->error( "{}: GlobalDB not started", __func__ );
            return outcome::failure( Error::GLOBALDB_NOT_STARTED );
        }

        // Unlike the compaction call, the sizes API has NO unbounded
        // range: an empty-to-empty range is the EMPTY range, so an
        // explicit sentinel high bound spans every real key class (binary
        // CIDv1, '/', ':', and base58 keys all sort below 0xFF bytes).
        // The sentinel string must OUTLIVE the range: Slice borrows the
        // buffer, it does not copy it.
        const std::string sentinel_high( 16, '\xFF' );
        const auto        whole_db = ::ROCKSDB_NAMESPACE::Range( ::ROCKSDB_NAMESPACE::Slice(),
                                                          ::ROCKSDB_NAMESPACE::Slice( sentinel_high ) );

        // Files AND memtables are counted: a node under the 32MB write
        // buffer keeps everything in the memtable, where a files-only
        // read would report ~0. The sentinel and flags live ONLY here
        // (DRY invariant).
        const auto flags = ::ROCKSDB_NAMESPACE::DB::SizeApproximationFlags::INCLUDE_FILES |
                           ::ROCKSDB_NAMESPACE::DB::SizeApproximationFlags::INCLUDE_MEMTABLES;

        uint64_t   bytes  = 0;
        const auto status = datastore->getDB()->GetApproximateSizes( &whole_db, 1, &bytes, flags );
        if ( !status.ok() )
        {
            m_logger->warn( "{}: GetApproximateSizes failed: {}", __func__, status.ToString() );
            return outcome::failure( Error::ROCKSDB_IO );
        }
        return bytes;
    }

    std::shared_ptr<CRDTWorkJournal> GlobalDB::GetWorkJournal() const
    {
        auto crdt_datastore = ActiveCRDTDataStore();
        return crdt_datastore ? crdt_datastore->GetWorkJournal() : nullptr;
    }

    outcome::result<GlobalDB::CRDTHeadListResult> GlobalDB::GetCRDTHeadList()
    {
        auto crdt_datastore = ActiveCRDTDataStore();
        if ( !crdt_datastore )
        {
            return outcome::failure( std::errc::operation_canceled );
        }
        return crdt_datastore->GetHeadList();
    }

    outcome::result<uint64_t> GlobalDB::GetKeyPriority( const std::string &key ) const
    {
        auto crdt_datastore = ActiveCRDTDataStore();
        if ( !crdt_datastore )
        {
            return outcome::failure( std::errc::operation_canceled );
        }
        return crdt_datastore->GetPriority( key );
    }

    outcome::result<uint64_t> GlobalDB::GetCRDTHeadHeight( const CID &aCid, const std::string &topic )
    {
        auto crdt_datastore = ActiveCRDTDataStore();
        if ( !crdt_datastore )
        {
            return outcome::failure( std::errc::operation_canceled );
        }
        return crdt_datastore->GetHeadHeight( aCid, topic );
    }

    outcome::result<void> GlobalDB::CRDTHeadRemove( const CID &aCid, const std::string &topic )
    {
        auto crdt_datastore = ActiveCRDTDataStore();
        if ( !crdt_datastore )
        {
            return outcome::failure( std::errc::operation_canceled );
        }
        return crdt_datastore->RemoveHead( aCid, topic );
    }

    outcome::result<void> GlobalDB::CRDTHeadAdd( const CID &aCid, const std::string &topic, uint64_t priority )
    {
        auto crdt_datastore = ActiveCRDTDataStore();
        if ( !crdt_datastore )
        {
            return outcome::failure( std::errc::operation_canceled );
        }
        return crdt_datastore->AddHead( aCid, topic, priority );
    }

    std::shared_ptr<PubSubBroadcasterExt> GlobalDB::GetBroadcaster()
    {
        return ActiveBroadcaster();
    }

    outcome::result<CrdtDatastore::JobStatus> GlobalDB::GetCIDJobStatus( const CID &cid ) const
    {
        auto crdt_datastore = ActiveCRDTDataStore();
        if ( !crdt_datastore )
        {
            return outcome::failure( Error::CRDT_DATASTORE_NOT_CREATED );
        }
        return crdt_datastore->GetJobStatus( cid );
    }

    outcome::result<void> GlobalDB::RequestHeadBroadcast( const std::set<std::string> &topics )
    {
        auto crdt_datastore = ActiveCRDTDataStore();
        if ( !crdt_datastore )
        {
            m_logger->error( "{}: CRDT datastore not initialized", __func__ );
            return outcome::failure( Error::CRDT_DATASTORE_NOT_CREATED );
        }
        if ( !cid_receiving_started_ )
        {
            m_logger->error( "{}: Broadcaster not receiving yet", __func__ );
            return outcome::failure( Error::CRDT_DATASTORE_NOT_CREATED );
        }
        if ( !cid_sync_started_ )
        {
            m_logger->error( "{}: CRDT not syncing", __func__ );
            return outcome::failure( Error::CRDT_DATASTORE_NOT_CREATED );
        }

        if ( !started_.load() )
        {
            m_logger->error( "{}: GlobalDB not started", __func__ );
            return outcome::failure( Error::GLOBALDB_NOT_STARTED );
        }

        m_logger->debug( "{}: Forwarding request for {} topics", __func__, topics.size() );
        return crdt_datastore->BroadcastHeadsForTopics( topics );
    }

    outcome::result<std::unordered_set<std::string>> GlobalDB::GetMonitoredTopics() const
    {
        auto crdt_datastore = ActiveCRDTDataStore();
        if ( !crdt_datastore )
        {
            m_logger->error( "{}: CRDT datastore not initialized", __func__ );
            return outcome::failure( Error::CRDT_DATASTORE_NOT_CREATED );
        }
        m_logger->debug( "{}: Forwarding request for topics", __func__ );
        return crdt_datastore->GetTopicNames();
    }

    std::shared_ptr<crdt::CrdtDatastore> GlobalDB::GetCRDTDataStore()
    {
        return ActiveCRDTDataStore();
    }

    outcome::result<std::vector<std::pair<std::string, base::Buffer>>> GlobalDB::GetLocalDeltaKeyValues(
        const std::string &cid_string )
    {
        auto crdt_datastore = ActiveCRDTDataStore();
        if ( !crdt_datastore )
        {
            m_logger->error( "{}: CRDT datastore not initialized", __func__ );
            return outcome::failure( Error::CRDT_DATASTORE_NOT_CREATED );
        }
        return crdt_datastore->GetLocalDeltaKeyValues( cid_string );
    }

    outcome::result<std::vector<std::string>> GlobalDB::GetLocalDeltaKeys( const std::string &cid_string ) const
    {
        auto crdt_datastore = ActiveCRDTDataStore();
        if ( !crdt_datastore )
        {
            m_logger->error( "{}: CRDT datastore not initialized", __func__ );
            return outcome::failure( Error::CRDT_DATASTORE_NOT_CREATED );
        }
        return crdt_datastore->GetLocalDeltaKeys( cid_string );
    }

    outcome::result<bool> GlobalDB::HasLocalBlock( const std::string &cid_string ) const
    {
        if ( !m_dagSyncer )
        {
            m_logger->error( "{}: DAG syncer not initialized", __func__ );
            return outcome::failure( Error::GLOBALDB_NOT_STARTED );
        }
        BOOST_OUTCOME_TRY( auto cid, CID::fromString( cid_string ) );
        return m_dagSyncer->HasRawBlockBytes( cid );
    }
}
