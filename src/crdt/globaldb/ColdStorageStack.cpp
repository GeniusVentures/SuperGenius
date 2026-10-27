/**
 * @file       ColdStorageStack.cpp
 * @brief      Real cold-storage stack assembly (SPILL-01, v1.1 Phase 8).
 *
 * COMPILED ONLY UNDER SGNS_COLD_STORAGE: this is the first production
 * SuperGenius TU that links sgns-blockstore. The sibling
 * ColdStorageStackStub.cpp carries the OFF-build behavior.
 *
 * Assembly order mirrors thirdparty/sgns-blockstore/test/upload_drainer_test.cpp
 * (LocalBlockStore -> S3 client -> S3BlockStore), with the decorator wrapped
 * around the injected inner datastore. Every library result is guarded with
 * has_value() BEFORE .value()/.error() - the library result alias uses
 * policy::terminate (Cid.hpp landmine).
 */

#include "ColdStorageStack.hpp"

#include <algorithm>
#include <filesystem>

#include <sgns-blockstore/LocalBlockStore.hpp>
#include <sgns-blockstore/S3AdminClient.hpp>
#include <sgns-blockstore/S3BlockStore.hpp>
#include <sgns-blockstore/S3Client.hpp>

#include "BlockStoreBackedIpfsDatastore.hpp"
#include "ColdStorageManager.hpp"
#include "ColdReadService.hpp"

namespace sgns::crdt
{
    namespace
    {
        /// Cold-store directory name under the database path - a sibling of
        /// "backups", NEVER inside it (M3 D-09: the journal is rebuildable,
        /// not backed up).
        constexpr const char *kColdStoreDirectoryName = "coldstore";

        /// Built-in backpressure defaults for programmatic (non-parsed)
        /// ColdStorageOptions whose knobs are still 0 (parser normalizes
        /// file-sourced options; the factory defends the direct path).
        constexpr uint32_t kDefaultDrainIntervalMs = 5000;
        constexpr uint32_t kDefaultBackoffCapMs    = 60000;
        constexpr uint32_t kDefaultWarnPending     = 10000;
        constexpr uint32_t kDefaultCriticalPending = 100000;

        /// Stats read budget for the stack-constructed admin client
        /// (compile-time; the flush budgets live on the manager as
        /// kCadenceFlushReadTimeoutMs / kFinalFlushBudgetMs).
        constexpr uint32_t kAdminStatsReadTimeoutMs = 5000;

        /// Documented s3d default admin listener (D-06 - loopback).
        constexpr const char *kDefaultAdminEndpoint = "http://127.0.0.1:8001";
    } // namespace

    ColdStorageStackResult OpenColdStorageStack( const GlobalDB::ColdStorageOptions &options,
                                                 const std::string                   &database_path_absolute,
                                                 std::shared_ptr<ipfs_lite::ipfs::IpfsDatastore> inner )
    {
        if ( !options.enabled )
        {
            // Disabled config must never reach the real stack; the caller
            // routes disabled config around the factory entirely.
            return outcome::failure( std::string( "cold storage stack opened with disabled options" ) );
        }
        if ( inner == nullptr )
        {
            return outcome::failure( std::string( "cold storage stack requires a non-null inner datastore" ) );
        }

        // The library Open factories throw std::invalid_argument for
        // empty endpoint/bucket/path (programmer-error taxonomy) - a
        // programmatic ColdStorageOptions that bypassed the parser must
        // yield a loud outcome failure, never a throw through the embedder
        // (08-REVIEW MA-02).
        try
        {
            // Sibling coldstore directory (ResolveBackupDirectory derivation
            // shape; LocalBlockStore creates/opens its own RocksDB there).
            const std::string cold_store_directory =
                ( std::filesystem::path( database_path_absolute ) / kColdStoreDirectoryName ).string();

            auto opened_local = sgns::blockstore::LocalBlockStore::Open(
                sgns::blockstore::LocalBlockStoreOptions{ cold_store_directory } );
            if ( !opened_local.has_value() )
            {
                // BlockStatus detail only - never a credential value.
                return outcome::failure( "cold storage: LocalBlockStore open failed at '" + cold_store_directory
                                         + "' (" + opened_local.error().message + ")" );
            }
            std::shared_ptr<sgns::blockstore::LocalBlockStore> local_store = std::move( opened_local.value() );

            // Production S3 client: construction is eagerly validating with
            // zero network I/O (Phase-6 D-06) - a non-listening endpoint
            // cannot fail here.
            sgns::blockstore::S3ClientConfig client_config;
            client_config.endpoint  = options.endpoint;
            client_config.bucket    = options.bucket;
            client_config.accessKey = options.accessKey;
            client_config.secretKey = options.secretKey;
            client_config.pathStyle = options.pathStyle;
            client_config.useTls    = options.tls;
            auto opened_client      = sgns::blockstore::OpenMinioS3Client( client_config );
            if ( !opened_client.has_value() )
            {
                return outcome::failure( "cold storage: S3 client construction failed ("
                                         + opened_client.error().message + ")" );
            }

            auto opened_remote = sgns::blockstore::S3BlockStore::Open(
                opened_client.value(), sgns::blockstore::S3BlockStoreOptions{ "v1/blocks" } );
            if ( !opened_remote.has_value() )
            {
                return outcome::failure( "cold storage: S3BlockStore open failed ("
                                         + opened_remote.error().message + ")" );
            }
            std::shared_ptr<sgns::blockstore::S3BlockStore> remote_store = std::move( opened_remote.value() );

            // The ONE shared mutex serializing publisher vs drainer access to
            // the LocalBlockStore (library single-threaded-use contract).
            auto cold_store_mutex = std::make_shared<std::mutex>();

            // READ-01 (v1.1 Phase 9): the ONE shared mutex serializing ALL
            // S3BlockStore access - the cold-read chain's Gets race the
            // drainer's remote Puts from this point on (MinioS3Client has
            // no concurrent-call contract, 09-RESEARCH §6.3).
            auto remote_store_mutex = std::make_shared<std::mutex>();

            // The decorator and the drainer SHARE ownership of the journal
            // store and the mutex (copy the handles, never move).
            auto decorator = std::make_shared<BlockStoreBackedIpfsDatastore>(
                std::move( inner ), local_store, cold_store_mutex, sgns::base::createLogger( "ColdStorage" ) );

            // Drainer options: normalize any still-0 knob to its built-in
            // default (parsed configs arrive already normalized; programmatic
            // configs defend here), then enforce the pair rule loudly.
            GlobalDB::ColdStorageOptions::BackpressureOptions backpressure = options.backpressure;
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
                return outcome::failure( "cold storage: backpressure requires warn_pending < critical_pending "
                                         "(warn_pending="
                                         + std::to_string( backpressure.warn_pending )
                                         + ", critical_pending=" + std::to_string( backpressure.critical_pending )
                                         + ")" );
            }

            // Durability normalization defense (Phase 10, the backpressure
            // defense shape): 0-means-default to the manager's public
            // constants, then the min<=max rule loud on EFFECTIVE values.
            ColdStorageManager::DurabilityOptions durability;
            durability.flush_min_interval_ms = options.durability.flush_min_interval_ms;
            durability.flush_max_interval_ms = options.durability.flush_max_interval_ms;
            if ( durability.flush_min_interval_ms == 0 )
            {
                durability.flush_min_interval_ms =
                    static_cast<uint32_t>( ColdStorageManager::kDefaultFlushMinIntervalMs.count() );
            }
            if ( durability.flush_max_interval_ms == 0 )
            {
                durability.flush_max_interval_ms =
                    static_cast<uint32_t>( ColdStorageManager::kDefaultFlushMaxIntervalMs.count() );
            }
            if ( durability.flush_min_interval_ms > durability.flush_max_interval_ms )
            {
                return outcome::failure( "cold storage: durability requires flush_min_interval_ms <= "
                                         "flush_max_interval_ms (flush_min_interval_ms="
                                         + std::to_string( durability.flush_min_interval_ms )
                                         + ", flush_max_interval_ms="
                                         + std::to_string( durability.flush_max_interval_ms ) + ")" );
            }

            // s3d admin facade (D-05/D-06): constructed beside the S3 client
            // INSIDE the same try/catch - eager validation, zero network
            // I/O; a construction failure is an outcome failure, never a
            // throw escape. Empty endpoint = the documented s3d default
            // (loopback listener). The password is env-only and never
            // logged (redaction by omission).
            sgns::blockstore::S3AdminClientConfig admin_config;
            admin_config.endpoint              = options.admin_endpoint.empty()
                                                    ? kDefaultAdminEndpoint
                                                    : options.admin_endpoint;
            admin_config.adminPassword         = options.adminPassword;
            admin_config.tlsVerify             = true;
            admin_config.stats_read_timeout_ms = kAdminStatsReadTimeoutMs;
            auto opened_admin                  = sgns::blockstore::OpenS3dAdminClient( admin_config );
            if ( !opened_admin.has_value() )
            {
                return outcome::failure( "cold storage: admin client construction failed ("
                                         + opened_admin.error().message + ")" );
            }
            auto admin_client = opened_admin.value();

            // GC-01 (v1.1 Phase 11): retention posture conversion - the
            // parse layer already normalized the cadence knobs (and the
            // manager ctor re-defends), so this is the plain field copy of
            // the durability conversion shape. retention_age_ms == 0 means
            // the GC never runs (the D-02 absent=off posture).
            ColdStorageManager::RetentionOptions retention;
            retention.retention_age_ms = options.retention.retention_age_ms;
            retention.dry_run          = options.retention.dry_run;
            retention.gc_interval_ms   = options.retention.gc_interval_ms;
            retention.grace_ms         = options.retention.grace_ms;

            auto manager = std::make_shared<ColdStorageManager>(
                local_store, remote_store, cold_store_mutex, remote_store_mutex, backpressure, durability,
                admin_client, retention );
            // Missing-password posture (planner ruling 1 / 10-RESEARCH
            // §5.3): default-on flush - the manager emits ONE startup WARN
            // naming the env var; never a hard fail, never silent.
            manager->SetAdminPasswordPresent( !options.adminPassword.empty() );

            // READ-01 (v1.1 Phase 9): the cold-read chain service over the
            // SAME store handles and mutexes, rehydrating through the
            // decorator (its set() journals + writes hot - SC4's path).
            auto cold_read_service = std::make_shared<ColdReadService>(
                local_store, remote_store, cold_store_mutex, remote_store_mutex, decorator );

            ColdStorageBundle bundle;
            bundle.data_store         = std::move( decorator );
            bundle.manager            = std::move( manager );
            bundle.cold_store_mutex   = std::move( cold_store_mutex );
            bundle.remote_store_mutex = std::move( remote_store_mutex );
            // The SAME service object, up-cast to the ungated hook
            // interface for GraphsyncDAGSyncer injection (globaldb.cpp
            // never needs the complete gated type).
            bundle.cold_read_service  = cold_read_service;
            bundle.cold_read_hook     = std::move( cold_read_service );
            return outcome::success( std::move( bundle ) );
        }
        catch ( const std::invalid_argument &e )
        {
            return outcome::failure( std::string( "cold storage stack rejected configuration: " ) + e.what() );
        }
    }

    void StartColdStorageManager( const std::shared_ptr<ColdStorageManager> &manager )
    {
        if ( manager )
        {
            manager->Start();
        }
    }

    void AttachColdStorageGc( const std::shared_ptr<ColdStorageManager> &manager,
                              CrdtDatastore                             *crdt_datastore,
                              GraphsyncDAGSyncer                         *dag_syncer,
                              std::shared_ptr<HotStoreFace>               hot_store )
    {
        // GC-01 (v1.1 Phase 11) null-safe forwarder: disabled configs and
        // OFF builds carry a null manager; the manager itself rejects an
        // incomplete dependency set (fail-closed, never partial attach).
        if ( manager )
        {
            manager->AttachGcDependencies( crdt_datastore, dag_syncer, std::move( hot_store ) );
        }
    }

    void StopColdStorageManager( const std::shared_ptr<ColdStorageManager> &manager )
    {
        if ( manager )
        {
            manager->Stop();
        }
    }

    ColdStorageStatus GetColdStorageManagerStatus( const std::shared_ptr<ColdStorageManager> &manager )
    {
        return manager ? manager->GetStatus() : ColdStorageStatus{};
    }

    ColdUploadState GetColdUploadState( const std::shared_ptr<ColdStorageManager> &manager,
                                        const CID                                &cid )
    {
        if ( !manager )
        {
            return ColdUploadState::NoRecord;
        }
        // Gated TU: the library result type is legal HERE and maps to the
        // ungated enum before it crosses back (Pitfall 5). sgns::CID and
        // the library Cid alias the same libp2p type - direct pass.
        // MI-02 (Phase 11): absence and unreadability are DISTINCT - a
        // NotFound error is the clean NoRecord case; ANY other read
        // failure maps to Unreadable (never conflated with absence, never
        // healthy-by-omission).
        const auto state = manager->GetUploadState( cid );
        if ( !state.has_value() )
        {
            if ( state.error().code == sgns::blockstore::StatusClass::NotFound )
            {
                return ColdUploadState::NoRecord;
            }
            return ColdUploadState::Unreadable;
        }
        switch ( state.value() )
        {
            case sgns::blockstore::UploadState::PendingUpload:
                return ColdUploadState::Pending;
            case sgns::blockstore::UploadState::UploadedAccepted:
                return ColdUploadState::Accepted;
            case sgns::blockstore::UploadState::UploadedDurable:
                return ColdUploadState::Durable;
        }
        return ColdUploadState::NoRecord; // unreachable - defensive
    }

    void RequestColdStorageDrain( const std::shared_ptr<ColdStorageManager> &manager )
    {
        if ( manager )
        {
            manager->RequestDrain();
        }
    }

    void StartColdReadService( const std::shared_ptr<ColdReadService> &service )
    {
        if ( service )
        {
            service->Start();
        }
    }

    void StopColdReadService( const std::shared_ptr<ColdReadService> &service )
    {
        if ( service )
        {
            service->Stop();
        }
    }

} // namespace sgns::crdt
