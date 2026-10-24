#include "globaldb.hpp"
#include "ColdStorageStack.hpp"
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

#include <rapidjson/document.h>

#include <cstdlib>
#include <vector>

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
        case ProofError::COLD_STORAGE_INIT_FAILED:
            return "Cold storage stack initialization failed";
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

    GlobalDB::ColdStorageEnvLookup GlobalDB::DefaultColdStorageEnvLookup()
    {
        // D-07: credentials only - exactly two variables exist. Unset OR
        // empty reads as empty (= no override); env never activates (D-08).
        ColdStorageEnvLookup lookup;
        const char *access_env = std::getenv( "SGNS_BLOCKSTORE_S3_ACCESS_KEY" );
        const char *secret_env = std::getenv( "SGNS_BLOCKSTORE_S3_SECRET_KEY" );
        if ( access_env != nullptr && *access_env != '\0' )
        {
            lookup.access_key = access_env;
        }
        if ( secret_env != nullptr && *secret_env != '\0' )
        {
            lookup.secret_key = secret_env;
        }
        return lookup;
    }

    std::string GlobalDB::ColdStorageOptions::LogSummary() const
    {
        // T-7-03 redaction by omission: credential members are NEVER
        // referenced here - endpoint/bucket/prefix/path_style/tls plus the
        // four backpressure knobs (D-08 watermark class) only.
        return "endpoint=" + endpoint + ", bucket=" + bucket + ", prefix=" + prefix
             + ", path_style=" + ( pathStyle ? "true" : "false" ) + ", tls=" + ( tls ? "true" : "false" )
             + ", drain_interval_ms=" + std::to_string( backpressure.drain_interval_ms )
             + ", backoff_cap_ms=" + std::to_string( backpressure.backoff_cap_ms )
             + ", warn_pending=" + std::to_string( backpressure.warn_pending )
             + ", critical_pending=" + std::to_string( backpressure.critical_pending );
    }

    GlobalDB::ColdStorageParseResult GlobalDB::ParseColdStorageConfig( const std::string &crdt_config_text )
    {
        return ParseColdStorageConfig( crdt_config_text, DefaultColdStorageEnvLookup() );
    }

    GlobalDB::ColdStorageParseResult GlobalDB::ParseColdStorageConfig( const std::string         &crdt_config_text,
                                                                       const ColdStorageEnvLookup &env_lookup )
    {
        // Built-in backpressure defaults (D-07 discretion resolved per
        // 08-RESEARCH A1; 0 on any knob means its named default).
        constexpr uint32_t kDefaultDrainIntervalMs  = 5000;
        constexpr uint32_t kDefaultBackoffCapMs     = 60000;
        constexpr uint32_t kDefaultWarnPending      = 10000;
        constexpr uint32_t kDefaultCriticalPending  = 100000;

        ColdStorageOptions options; // disabled default (enabled == false)

        rapidjson::Document document;
        document.Parse( crdt_config_text.c_str() );

        // (a) D-06: whole-file failures keep the legacy lenient behavior -
        // disabled, NOT an error (the caller warns + keeps defaults).
        if ( document.HasParseError() || !document.IsObject() )
        {
            return options;
        }

        // (b) D-03: absent section => disabled.
        const auto section_it = document.FindMember( "blockstore" );
        if ( section_it == document.MemberEnd() )
        {
            return options;
        }

        // (c) present-but-not-an-object is a detectable malformation: loud.
        const rapidjson::Value &section = section_it->value;
        if ( !section.IsObject() )
        {
            return outcome::failure( std::string( "crdt_config.json: 'blockstore' section must be an object" ) );
        }

        // (d) single member pass: unknown key => loud (D-05); wrong-typed
        // known key => loud naming the key. Never warn-and-skip (P10).
        std::string file_endpoint;
        std::string file_bucket;
        std::string file_access_key;
        std::string file_secret_key;
        std::string file_prefix;
        bool        path_style = true;
        bool        tls        = false;
        ColdStorageOptions::BackpressureOptions backpressure;
        for ( auto member = section.MemberBegin(); member != section.MemberEnd(); ++member )
        {
            const std::string key = member->name.GetString();

            // (d1) D-06: nested "backpressure" sub-object - present-but-
            // not-object is loud; inside it a strict single-member loop
            // accepts ONLY the four knobs (unknown key / wrong type loud,
            // naming blockstore.backpressure.<key>).
            if ( key == "backpressure" )
            {
                if ( !member->value.IsObject() )
                {
                    return outcome::failure(
                        std::string( "crdt_config.json: 'blockstore.backpressure' must be an object" ) );
                }
                for ( auto sub = member->value.MemberBegin(); sub != member->value.MemberEnd(); ++sub )
                {
                    const std::string sub_key = sub->name.GetString();
                    const bool is_uint_key = ( sub_key == "drain_interval_ms" || sub_key == "warn_pending"
                                               || sub_key == "critical_pending" || sub_key == "backoff_cap_ms" );
                    if ( !is_uint_key )
                    {
                        return outcome::failure(
                            "crdt_config.json: unknown key 'blockstore.backpressure." + sub_key + "'" );
                    }
                    if ( !sub->value.IsUint() )
                    {
                        return outcome::failure( "crdt_config.json: 'blockstore.backpressure." + sub_key
                                                 + "' must be a uint" );
                    }
                    const uint32_t value = sub->value.GetUint();
                    if ( sub_key == "drain_interval_ms" )
                    {
                        backpressure.drain_interval_ms = value;
                    }
                    else if ( sub_key == "warn_pending" )
                    {
                        backpressure.warn_pending = value;
                    }
                    else if ( sub_key == "critical_pending" )
                    {
                        backpressure.critical_pending = value;
                    }
                    else
                    {
                        backpressure.backoff_cap_ms = value;
                    }
                }
                continue; // handled - never falls into the string/bool checks
            }

            const bool is_string_key =
                ( key == "endpoint" || key == "bucket" || key == "access_key" || key == "secret_key" || key == "prefix" );
            const bool is_bool_key = ( key == "path_style" || key == "tls" );
            if ( !is_string_key && !is_bool_key )
            {
                return outcome::failure( "crdt_config.json: unknown key 'blockstore." + key + "'" );
            }
            if ( is_string_key && !member->value.IsString() )
            {
                return outcome::failure( "crdt_config.json: 'blockstore." + key + "' must be a string" );
            }
            if ( is_bool_key && !member->value.IsBool() )
            {
                return outcome::failure( "crdt_config.json: 'blockstore." + key + "' must be a boolean" );
            }
            if ( key == "endpoint" )
            {
                file_endpoint = member->value.GetString();
            }
            else if ( key == "bucket" )
            {
                file_bucket = member->value.GetString();
            }
            else if ( key == "access_key" )
            {
                file_access_key = member->value.GetString();
            }
            else if ( key == "secret_key" )
            {
                file_secret_key = member->value.GetString();
            }
            else if ( key == "prefix" )
            {
                file_prefix = member->value.GetString();
            }
            else if ( key == "path_style" )
            {
                path_style = member->value.GetBool();
            }
            else if ( key == "tls" )
            {
                tls = member->value.GetBool();
            }
        }

        // (d2) D-07 backpressure normalization: 0 on any knob means its
        // built-in default; then the pair rule - warn_pending must stay
        // STRICTLY below critical_pending, validated POST-normalization so
        // mixed 0/explicit forms are judged on their effective values.
        if ( backpressure.drain_interval_ms == 0 )
        {
            backpressure.drain_interval_ms = kDefaultDrainIntervalMs;
        }
        if ( backpressure.backoff_cap_ms == 0 )
        {
            backpressure.backoff_cap_ms = kDefaultBackoffCapMs;
        }
        if ( backpressure.warn_pending == 0 )
        {
            backpressure.warn_pending = kDefaultWarnPending;
        }
        if ( backpressure.critical_pending == 0 )
        {
            backpressure.critical_pending = kDefaultCriticalPending;
        }
        if ( backpressure.warn_pending >= backpressure.critical_pending )
        {
            return outcome::failure( "crdt_config.json: 'blockstore.backpressure' requires warn_pending < "
                                     "critical_pending (warn_pending="
                                     + std::to_string( backpressure.warn_pending )
                                     + ", critical_pending="
                                     + std::to_string( backpressure.critical_pending ) + ")" );
        }
        options.backpressure = backpressure;

        // (e) D-08 file-intent gate: all four required fields blank in the
        // FILE => disabled REGARDLESS of env (env overrides values, never
        // activates). An optional-fields-only section lands here too.
        const bool any_required_in_file =
            !file_endpoint.empty() || !file_bucket.empty() || !file_access_key.empty() || !file_secret_key.empty();
        if ( !any_required_in_file )
        {
            return options;
        }

        // (f) intent signalled: apply env VALUE overrides (D-07 - credentials
        // only; endpoint/bucket/prefix/path_style/tls come solely from the
        // file), THEN run the required-field check on post-env values (D-04).
        const std::string resolved_access_key = env_lookup.access_key.empty() ? file_access_key : env_lookup.access_key;
        const std::string resolved_secret_key = env_lookup.secret_key.empty() ? file_secret_key : env_lookup.secret_key;

        std::vector<std::string> missing;
        if ( file_endpoint.empty() )
        {
            missing.push_back( "endpoint" );
        }
        if ( file_bucket.empty() )
        {
            missing.push_back( "bucket" );
        }
        if ( resolved_access_key.empty() )
        {
            missing.push_back( "access_key (file or SGNS_BLOCKSTORE_S3_ACCESS_KEY)" );
        }
        if ( resolved_secret_key.empty() )
        {
            missing.push_back( "secret_key (file or SGNS_BLOCKSTORE_S3_SECRET_KEY)" );
        }
        if ( !missing.empty() )
        {
            std::string joined = missing[0];
            for ( size_t i = 1; i < missing.size(); ++i )
            {
                joined += ", " + missing[i];
            }
            return outcome::failure(
                "crdt_config.json: 'blockstore' partially filled - missing required field(s): " + joined );
        }

        // (g) enabled: populate with post-env values; optional defaults
        // (prefix empty, path_style true, tls false) unless explicitly set.
        options.enabled   = true;
        options.endpoint  = file_endpoint;
        options.bucket    = file_bucket;
        options.accessKey = resolved_access_key;
        options.secretKey = resolved_secret_key;
        options.prefix    = file_prefix;
        options.pathStyle = path_style;
        options.tls       = tls;
        return options;
    }

    outcome::result<std::shared_ptr<GlobalDB>> GlobalDB::New(
        std::shared_ptr<boost::asio::io_context>                              context,
        std::string                                                           databasePath,
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub>                      pubsub,
        std::shared_ptr<CrdtOptions>                                          crdtOptions,
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Network>            graphsyncnetwork,
        std::shared_ptr<libp2p::basic::Scheduler>                             scheduler,
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
        std::shared_ptr<RocksDB>                                              datastore,
            BackupOptions                                                         backup_options,
            ColdStorageOptions                                                    cold_storage_options )
    {
        if ( ( !context ) || ( !generator ) || ( !pubsub ) || ( !graphsyncnetwork ) )
        {
            return outcome::failure( Error::INVALID_PARAMETERS );
        }
        auto new_instance = std::shared_ptr<GlobalDB>(
            new GlobalDB( std::move( context ), std::move( databasePath ), std::move( pubsub ) ) );
        new_instance->backup_options_       = backup_options;
        new_instance->cold_storage_options_ = cold_storage_options;
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
        // D-11 (v1.1 Phase 8): the drainer stops IMMEDIATELY AFTER the
        // backup loop and BEFORE the lifecycle_mutex_ teardown block -
        // bounded grace then join while the stores still live.
        StopColdStorageManager();

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

        // Cold-storage seam (SPILL-01, v1.1 Phase 8): ONLY when enabled,
        // wrap the freshly constructed inner datastore in the gated
        // journal-spill decorator and hand THE DECORATOR to the DAG syncer
        // below. When disabled, the inner store flows to the syncer
        // untouched - byte-identical to the pre-Phase-8 construction.
        std::shared_ptr<ipfs_lite::ipfs::IpfsDatastore> block_datastore = ipfsDataStore;
        if ( cold_storage_options_.enabled )
        {
            const auto database_path_absolute = boost::filesystem::absolute( m_databasePath ).string();
            auto        stack = OpenColdStorageStack( cold_storage_options_, database_path_absolute, ipfsDataStore );
            if ( !stack.has_value() )
            {
                m_logger->error( "Cold storage stack initialization failed: {}", stack.error() );
                return outcome::failure( Error::COLD_STORAGE_INIT_FAILED );
            }
            block_datastore   = stack.value().data_store;
            cold_data_store_  = block_datastore;
            cold_manager_     = std::move( stack.value().manager );
            cold_store_mutex_ = std::move( stack.value().cold_store_mutex );
        }

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
        auto dagSyncer = std::make_shared<GraphsyncDAGSyncer>( block_datastore, graphsync, host );

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

    ColdStorageStatus GlobalDB::GetColdStorageStatus() const
    {
        // Active-accessor pattern (ActiveDataStore shape): the default
        // triple once shutdown has started; otherwise the drainer's
        // atomics-backed status - no journal scan under the lock.
        std::lock_guard<std::mutex> lock( lifecycle_mutex_ );
        if ( shutdown_started_.load() )
        {
            return ColdStorageStatus{};
        }
        return GetColdStorageManagerStatus( cold_manager_ );
    }

    void GlobalDB::StartColdStorageManager()
    {
        sgns::crdt::StartColdStorageManager( cold_manager_ );
    }

    void GlobalDB::StopColdStorageManager()
    {
        sgns::crdt::StopColdStorageManager( cold_manager_ );
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
        StartColdStorageManager();
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

}
