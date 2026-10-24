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

#include <filesystem>

#include <sgns-blockstore/LocalBlockStore.hpp>
#include <sgns-blockstore/S3BlockStore.hpp>
#include <sgns-blockstore/S3Client.hpp>

#include "BlockStoreBackedIpfsDatastore.hpp"

namespace sgns::crdt
{
    namespace
    {
        /// Cold-store directory name under the database path - a sibling of
        /// "backups", NEVER inside it (M3 D-09: the journal is rebuildable,
        /// not backed up).
        constexpr const char *kColdStoreDirectoryName = "coldstore";
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

        // The ONE shared mutex serializing publisher vs drainer access to
        // the LocalBlockStore (library single-threaded-use contract).
        auto cold_store_mutex = std::make_shared<std::mutex>();

        auto decorator = std::make_shared<BlockStoreBackedIpfsDatastore>(
            std::move( inner ), std::move( local_store ), cold_store_mutex, sgns::base::createLogger( "ColdStorage" ) );

        ColdStorageBundle bundle;
        bundle.data_store       = std::move( decorator );
        bundle.manager          = nullptr; // filled by plan 08-02
        bundle.cold_store_mutex = std::move( cold_store_mutex );
        return outcome::success( std::move( bundle ) );
    }

    void StartColdStorageManager( const std::shared_ptr<ColdStorageManager> &manager )
    {
        (void)manager; // 08-02 fills the real bridge; null-safe no-op here
    }

    void StopColdStorageManager( const std::shared_ptr<ColdStorageManager> &manager )
    {
        (void)manager; // 08-02 fills the real bridge; null-safe no-op here
    }

    ColdStorageStatus GetColdStorageManagerStatus( const std::shared_ptr<ColdStorageManager> &manager )
    {
        (void)manager; // 08-02 fills the real bridge; default triple for now
        return ColdStorageStatus{};
    }

    void RequestColdStorageDrain( const std::shared_ptr<ColdStorageManager> &manager )
    {
        (void)manager; // 08-02 fills the real bridge; null-safe no-op here
    }

} // namespace sgns::crdt
