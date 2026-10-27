/**
 * @file       ColdStorageStackStub.cpp
 * @brief      OFF-build cold-storage stack stub (SPILL-01, v1.1 Phase 8).
 *
 * COMPILED WHEN SGNS_COLD_STORAGE IS OFF (default, mobile, downstream
 * builds): zero sgns-blockstore symbols, zero library includes. A config
 * that requests cold storage on an OFF build fails LOUDLY - the silent
 * disable trap (08-RESEARCH P10) is exactly what this message exists to
 * prevent; a disabled config gets the pure passthrough bundle so the
 * caller's disabled path stays byte-identical to pre-Phase-8 behavior.
 */

#include "ColdStorageStack.hpp"

namespace sgns::crdt
{
    ColdStorageStackResult OpenColdStorageStack( const GlobalDB::ColdStorageOptions &options,
                                                 const std::string                   &database_path_absolute,
                                                 std::shared_ptr<ipfs_lite::ipfs::IpfsDatastore> inner )
    {
        (void)database_path_absolute;
        if ( options.enabled )
        {
            // Loud failure naming the build gate - never a silent disable.
            return outcome::failure( std::string(
                "binary built without SGNS_COLD_STORAGE: blockstore section requests cold storage "
                "but support is not compiled - rebuild with -DSGNS_COLD_STORAGE=ON" ) );
        }
        ColdStorageBundle bundle;
        bundle.data_store         = std::move( inner ); // pure passthrough
        bundle.manager            = nullptr;
        bundle.cold_store_mutex   = nullptr;
        // READ-01 (v1.1 Phase 9): no cold-read service exists on OFF
        // builds - the DAGSyncer hook stays null (byte-identical reads).
        bundle.remote_store_mutex = nullptr;
        bundle.cold_read_service  = nullptr;
        bundle.cold_read_hook     = nullptr;
        return outcome::success( std::move( bundle ) );
    }

    void StartColdStorageManager( const std::shared_ptr<ColdStorageManager> &manager )
    {
        (void)manager; // no manager exists on OFF builds; null-safe no-op
    }

    void StopColdStorageManager( const std::shared_ptr<ColdStorageManager> &manager )
    {
        (void)manager; // no manager exists on OFF builds; null-safe no-op
    }

    ColdStorageStatus GetColdStorageManagerStatus( const std::shared_ptr<ColdStorageManager> &manager )
    {
        (void)manager; // no manager exists on OFF builds; default triple
        return ColdStorageStatus{};
    }

    // D-09 (v1.1 Phase 10): the ODR twin of the gated bridge - OFF builds
    // have no journal at all, so every query reads NoRecord.
    ColdUploadState GetColdUploadState( const std::shared_ptr<ColdStorageManager> &manager,
                                        const CID                                &cid )
    {
        (void)manager;
        (void)cid;
        return ColdUploadState::NoRecord;
    }

    void RequestColdStorageDrain( const std::shared_ptr<ColdStorageManager> &manager )
    {
        (void)manager; // no manager exists on OFF builds; null-safe no-op
    }

    void StartColdReadService( const std::shared_ptr<ColdReadService> &service )
    {
        (void)service; // no service exists on OFF builds; null-safe no-op
    }

    void StopColdReadService( const std::shared_ptr<ColdReadService> &service )
    {
        (void)service; // no service exists on OFF builds; null-safe no-op
    }

} // namespace sgns::crdt
