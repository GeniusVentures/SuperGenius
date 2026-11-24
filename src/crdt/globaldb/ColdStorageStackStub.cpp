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
    // TEST-ONLY override setter (12-01 Task 3): no stack exists on OFF
    // builds, so there is nothing to override - the state lives in the
    // gated TU. PRODUCTION CODE MUST NEVER CALL this.
    void SetColdStorageStackClientsForTesting(
        std::shared_ptr<sgns::blockstore::S3Client>     s3_client,
        std::shared_ptr<sgns::blockstore::S3AdminClient> admin_client )
    {
        (void)s3_client;
        (void)admin_client;
    }
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

    // GC-01 (v1.1 Phase 11): the ODR twin of the attach bridge - OFF
    // builds have no manager, so the attach is the null-safe no-op (the
    // retention GC itself cannot exist without the gated stack).
    void AttachColdStorageGc( const std::shared_ptr<ColdStorageManager> &manager,
                              CrdtDatastore                             *crdt_datastore,
                              GraphsyncDAGSyncer                         *dag_syncer,
                              std::shared_ptr<HotStoreFace>               hot_store )
    {
        (void)manager;
        (void)crdt_datastore;
        (void)dag_syncer;
        (void)hot_store; // no GC exists on OFF builds; null-safe no-op
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

    // COMP-03 (v1.2 Phase 16): the ODR twin of the disposed-marker
    // bridge - OFF builds have no cold tier, so nothing is ever disposed.
    // Mirrors the gated TU's null-manager arm exactly (false, never
    // nullopt - the caller's freeze-safe classification is for read
    // errors, not absent tiers).
    // [19-05 run-45252321451 find] This twin (and the three GC-hook twins
    // below, COMP-02) were MISSING when the forwardings landed - every
    // OFF-build consumer pulling the GlobalDB unity object (iOS/Android;
    // the dispatch instrument never gates -DSGNS_COLD_STORAGE onto them)
    // failed to link with undefined references to exactly these four.
    std::optional<bool> IsColdDisposed( const std::shared_ptr<ColdStorageManager> &manager,
                                        const CID                                &cid )
    {
        (void)manager;
        (void)cid; // no manager exists on OFF builds; nothing is disposed
        return false;
    }

    // COMP-02 (v1.2 Phase 16): the ODR twins of the TEST-ONLY GC
    // park-point forwardings - no manager exists on OFF builds, so there
    // is nothing to park. PRODUCTION CODE MUST NEVER CALL these.
    void SetGcBetweenHalvesHookForTesting( const std::shared_ptr<ColdStorageManager> &manager,
                                            std::function<void()>                    hook )
    {
        (void)manager;
        (void)hook; // no manager exists on OFF builds; null-safe no-op
    }

    void SetGcPreHalfOneHookForTesting( const std::shared_ptr<ColdStorageManager> &manager,
                                         std::function<void()>                    hook )
    {
        (void)manager;
        (void)hook; // no manager exists on OFF builds; null-safe no-op
    }

    void SetGcPreMarkProcessHookForTesting( const std::shared_ptr<ColdStorageManager> &manager,
                                             std::function<void()>                    hook )
    {
        (void)manager;
        (void)hook; // no manager exists on OFF builds; null-safe no-op
    }

    // [v1.2 Phase 19, the dispatch-4 tart find] The ODR twin of the
    // unconditional mid-pass park forwarding - no manager exists on OFF
    // builds, so there is nothing to park. PRODUCTION CODE MUST NEVER
    // CALL this.
    void SetGcMidPassHookForTesting( const std::shared_ptr<ColdStorageManager> &manager,
                                     std::function<void()>                    hook )
    {
        (void)manager;
        (void)hook; // no manager exists on OFF builds; null-safe no-op
    }

    void StartColdReadService( const std::shared_ptr<ColdReadService> &service )
    {
        (void)service; // no service exists on OFF builds; null-safe no-op
    }

    void StopColdReadService( const std::shared_ptr<ColdReadService> &service )
    {
        (void)service; // no service exists on OFF builds; null-safe no-op
    }

    // SERVE-03/D-04 (v1.2 Phase 15): the ODR twin of the install bridge -
    // no service exists on OFF builds, so there is nothing to arm (the
    // consult never runs; reads stay byte-identical to pre-phase).
    void SetPruneEvidenceLookupOnColdReadService(
        const std::shared_ptr<ColdReadService>           &service,
        std::function<PruneEvidenceClass( const CID & )>  lookup )
    {
        (void)service;
        (void)lookup; // no service exists on OFF builds; null-safe no-op
    }

} // namespace sgns::crdt
