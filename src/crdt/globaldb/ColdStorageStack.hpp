/**
 * @file       ColdStorageStack.hpp
 * @brief      Gated cold-storage stack factory + lifecycle bridges
 *             (SPILL-01, v1.1 Phase 8).
 *
 * ALWAYS COMPILED - both SGNS_COLD_STORAGE build configurations link this
 * TU set - and deliberately free of every sgns-blockstore type: the real
 * implementation lives in ColdStorageStack.cpp (compiled only under
 * SGNS_COLD_STORAGE) and the stub in ColdStorageStackStub.cpp (compiled
 * when OFF). Default builds must stay pristine: no library include, no
 * library symbol, no new code path (the Phase-7 byte-identical bar).
 *
 * globaldb.cpp reaches the stack ONLY through OpenColdStorageStack and the
 * four null-safe lifecycle bridges below - never through ColdStorageManager
 * methods directly - so the ungated translation units never see the
 * manager's library-typed interface.
 */

#ifndef SUPERGENIUS_CRDT_COLDSTORAGESTACK_HPP
#define SUPERGENIUS_CRDT_COLDSTORAGESTACK_HPP

#include <memory>
#include <mutex>
#include <string>

#include <ipfs_lite/ipfs/datastore.hpp>
#include <outcome/outcome.hpp>

#include "globaldb.hpp"

namespace sgns::crdt
{
    /**
     * @brief      Drainer watermark state over the pending-upload record
     *             count (D-02/D-08, v1.1 Phase 8).
     */
    enum class ColdStorageWatermark
    {
        None,     ///< below the warn threshold
        Warn,     ///< at/above warn_pending, below critical_pending
        Critical, ///< at/above critical_pending
    };

    /**
     * @brief      Per-CID cold-upload state, ungated mirror of the
     *             library's journal triple + the no-record case (D-09,
     *             v1.1 Phase 10 - the ColdStorageWatermark ungated-enum
     *             precedent: no sgns-blockstore type crosses this
     *             always-compiled header).
     */
    enum class ColdUploadState
    {
        NoRecord, ///< no journal record for the CID
        Pending,  ///< local commit, remote upload not yet acknowledged
        Accepted, ///< object store acknowledged receipt (durability unproven)
        Durable,  ///< flush-gated evidence observed (pendingObjects == 0)
        /// The journal read itself failed (IO/lock/corruption) - NEVER
        /// conflated with absence: the MI-02 widening (v1.1 Phase 11) so
        /// the retention GC's evidence gate can distinguish "no record"
        /// (not eligible, cleanly) from "unreadable" (not eligible,
        /// loudly - never healthy-by-omission).
        Unreadable,
    };

    /**
     * @brief      Operator-visible cold-storage status triple (D-08
     *             minimum): journal depth, watermark state, drainer health.
     *
     * Backed by drainer-maintained atomics - reading it never scans the
     * journal and never blocks the write path.
     */
    struct ColdStorageStatus
    {
        size_t              pending_upload_count{ 0 };                    ///< pending-upload record count
        ColdStorageWatermark watermark{ ColdStorageWatermark::None };     ///< current watermark
        bool                drainer_healthy{ false };                     ///< false while a failure streak is held
        // Flush-gated durability evidence (DUR-01, v1.1 Phase 10).
        // PLAIN TYPES ONLY (Pitfall 5 - no sgns-blockstore type crosses
        // this always-compiled header; the manager fills them from
        // atomics, so reading them never scans the journal).
        size_t   accepted_upload_count{ 0 };       ///< records at UploadedAccepted
        size_t   durable_upload_count{ 0 };        ///< records at UploadedDurable (evidence-backed)
        bool     last_flush_valid{ false };        ///< last observation completed (flush+stats succeeded)
        bool     last_flush_ok{ false };           ///< last observation met the evidence (pendingObjects == 0)
        uint64_t last_flush_unix_ms{ 0 };          ///< wall-clock ms of the last observation
        uint64_t last_flush_pending_objects{ 0 };  ///< pendingObjects observed by the last flush cycle
        /// True while a flush-failure streak holds the flush backoff - the
        /// D-07/Pitfall-6 separation observable: drain health and flush
        /// backoff vary INDEPENDENTLY (v1.1 Phase 10).
        bool     flush_backoff_active{ false };
        // SC4 staging health (D-12, v1.1 Phase 10): raw counters + the
        // conservative healthy flag - the GC-inhibition signal Phase 11
        // consumes. PLAIN TYPES ONLY (Pitfall 5 - the library UploadStats
        // flattens at this boundary).
        uint64_t staging_pending_objects{ 0 }; ///< s3d staged-object count (process-global)
        uint64_t staging_buffer_used{ 0 };     ///< staging disk buffer bytes in use
        uint64_t staging_buffer_limit{ 0 };    ///< staging buffer capacity (0 = unlimited)
        uint64_t staging_failed_uploads{ 0 };  ///< s3d permanent upload failures (cumulative)
        bool     staging_healthy{ false };     ///< conservative predicate (D-12) - false on unreadable evidence
        // Retention GC (GC-01, v1.1 Phase 11). PLAIN TYPES ONLY (Pitfall 5
        // - same always-compiled-header rule; the manager fills them from
        // gc status atomics). The evicting/reclaimed/disposed fields stay
        // zero until 11-02's destructive half lands.
        bool     gc_enabled{ false };                 ///< retention configured (age > 0) and the actor constructed
        bool     gc_dry_run{ false };                 ///< dry_run knob (suppresses deletes from 11-02 onward)
        bool     gc_inhibited_staging{ false };       ///< last tick skipped: staging unhealthy (D-12 gate)
        size_t   gc_evicting_count{ 0 };              ///< CIDs in the in-memory evicting mark set (11-02+)
        uint64_t gc_last_pass_unix_ms{ 0 };           ///< wall-clock ms of the last completed pass
        size_t   gc_last_pass_candidates{ 0 };        ///< cold-eligible CIDs the last pass identified
        size_t   gc_last_pass_reclaimed_count{ 0 };   ///< evicted CIDs the last pass deleted (11-02+)
        uint64_t gc_last_pass_reclaimed_bytes{ 0 };   ///< local bytes reclaimed by the last pass (11-02+)
        size_t   gc_disposed_marker_count{ 0 };       ///< disposed-CID markers currently persisted (11-02+)
    };

    // Defined in ColdStorageManager.hpp (gated TU); reachable here only
    // through the bridges below.
    class ColdStorageManager;

    // Defined in ColdReadService.hpp (gated TU, v1.1 Phase 9); reachable
    // here only through the bundle fields below.
    class ColdReadService;

    // Defined in crdt/cold_read_fallback.hpp (ungated interface, v1.1
    // Phase 9) - the DAGSyncer-side hook type. No sgns-blockstore type
    // crosses this header.
    class ColdReadFallback;

    // Forward declarations for the GC-dependency attach bridge (GC-01,
    // v1.1 Phase 11): the manager attaches AFTER construction, between
    // GlobalDB::Init's m_crdtDatastore creation and Start(). Raw
    // pointers/raw storage face only - no gated type crosses this
    // always-compiled header (the stub carries the ODR twin).
    class CrdtDatastore;
    class GraphsyncDAGSyncer;
    using HotStoreFace = storage::BufferStorage;

    /**
     * @brief      Everything GlobalDB owns of the cold-storage stack
     *             (v1.1 Phase 8; extended in Phase 9).
     *
     * data_store is the BlockStoreBackedIpfsDatastore decorator handed to
     * GraphsyncDAGSyncer in place of the inner RocksdbDatastore; manager is
     * null until Phase 8 plan 08-02 fills it (and stays null whenever cold
     * storage is disabled); cold_store_mutex is the ONE mutex shared by the
     * publisher (decorator set) and the drainer thread honoring the
     * library's single-threaded-use contract.
     *
     * Phase 9 (READ-01) additions: remote_store_mutex is the ONE mutex
     * serializing every S3BlockStore access (the cold-read chain's Gets
     * vs the drainer's remote Puts - MinioS3Client has no
     * concurrent-call contract); cold_read_service is the chain service
     * (creation-side up-cast - globaldb.cpp never needs the complete
     * gated type); cold_read_hook is the SAME object up-cast to the
     * ungated interface for GraphsyncDAGSyncer injection. Both stay null
     * on OFF builds and disabled configs.
     */
    struct ColdStorageBundle
    {
        std::shared_ptr<ipfs_lite::ipfs::IpfsDatastore> data_store;
        std::shared_ptr<ColdStorageManager>             manager;
        std::shared_ptr<std::mutex>                     cold_store_mutex;
        std::shared_ptr<std::mutex>                     remote_store_mutex;
        std::shared_ptr<ColdReadService>                cold_read_service;
        std::shared_ptr<ColdReadFallback>               cold_read_hook;
    };

    /// Factory outcome: the bundle on success, a human-readable failure
    /// message on error (never a credential value - redaction by omission).
    using ColdStorageStackResult = outcome::result<ColdStorageBundle, std::string>;

    /**
     * @brief       Open the cold-storage stack around an inner datastore.
     *
     * Real implementation (SGNS_COLD_STORAGE=ON): derives the cold-store
     * directory as the sibling directory "coldstore" under the database
     * path (NEVER inside the backups dir - M3 D-09 journal-rebuildable
     * posture), opens the LocalBlockStore journal, constructs the
     * production S3 client via OpenMinioS3Client (eager validation, zero
     * network I/O), opens the S3BlockStore, and wraps the inner datastore
     * in the journal-spill decorator.
     * @param[in]   options the enabled cold-storage configuration
     * @param[in]   database_path_absolute absolute database path (cold
     *              store derives as its "coldstore" sibling)
     * @param[in]   inner the freshly constructed RocksdbDatastore
     * @return      the bundle (manager null in plan 08-01), or a failure
     *              message
     */
    ColdStorageStackResult OpenColdStorageStack( const GlobalDB::ColdStorageOptions &options,
                                                 const std::string                   &database_path_absolute,
                                                 std::shared_ptr<ipfs_lite::ipfs::IpfsDatastore> inner );

    /**
     * @brief      Start the drainer thread (null-safe no-op when null).
     * @param[in]  manager the bundle's manager (may be null)
     */
    void StartColdStorageManager( const std::shared_ptr<ColdStorageManager> &manager );

    /**
     * @brief      Stop the drainer thread with bounded grace then join
     *             (null-safe no-op when null; D-11).
     * @param[in]  manager the bundle's manager (may be null)
     */
    void StopColdStorageManager( const std::shared_ptr<ColdStorageManager> &manager );

    /**
     * @brief      Read the D-08 status triple from the manager's atomics.
     * @param[in]  manager the bundle's manager (may be null)
     * @return     live status, or the default triple when null
     */
    ColdStorageStatus GetColdStorageManagerStatus( const std::shared_ptr<ColdStorageManager> &manager );

    /**
     * @brief      Null-safe per-CID upload-state bridge (D-09, v1.1
     *             Phase 10): maps the gated manager's library-typed
     *             pass-through onto the ungated ColdUploadState enum.
     * @param[in]  manager the bundle's manager (may be null)
     * @param[in]  cid the block whose journal state to read
     * @return     the mapped state, or NoRecord when null / no record
     */
    ColdUploadState GetColdUploadState( const std::shared_ptr<ColdStorageManager> &manager,
                                        const CID                                &cid );

    /**
     * @brief      Enqueue an ops-triggered drain (flag + notify only - the
     *             drain body NEVER runs on the caller's thread; SC2).
     * @param[in]  manager the bundle's manager (may be null)
     */
    void RequestColdStorageDrain( const std::shared_ptr<ColdStorageManager> &manager );

    /**
     * @brief      Null-safe GC-dependency attach bridge (GC-01, v1.1
     *             Phase 11 - the 6.1 post-construction seam).
     *
     * Real implementation (SGNS_COLD_STORAGE=ON): forwards to the
     * manager's AttachGcDependencies when BOTH the manager and every
     * dependency are non-null; the OFF-build stub is the ODR no-op twin.
     * globaldb.cpp calls this right after m_crdtDatastore is created and
     * BEFORE Start().
     * @param[in]  manager the bundle's manager (may be null - disabled or
     *             OFF build)
     * @param[in]  crdt_datastore the heads source (GetHeadList)
     * @param[in]  dag_syncer the DAG read path (GetNodeWithoutRequest)
     * @param[in]  hot_store the hot CRDT RocksDB face (the same handle
     *             GlobalDB owns; shared_ptr up-cast to the storage face)
     */
    void AttachColdStorageGc( const std::shared_ptr<ColdStorageManager> &manager,
                              CrdtDatastore                             *crdt_datastore,
                              GraphsyncDAGSyncer                         *dag_syncer,
                              std::shared_ptr<HotStoreFace>               hot_store );

    /**
     * @brief      Start the re-warm worker thread (null-safe no-op when
     *             null; READ-02, v1.1 Phase 9).
     * @param[in]  service the bundle's cold-read service (may be null)
     */
    void StartColdReadService( const std::shared_ptr<ColdReadService> &service );

    /**
     * @brief      Stop the re-warm worker with bounded grace then join
     *             (null-safe no-op when null; Phase-8 D-11 shape).
     * @param[in]  service the bundle's cold-read service (may be null)
     */
    void StopColdReadService( const std::shared_ptr<ColdReadService> &service );

} // namespace sgns::crdt

#endif // SUPERGENIUS_CRDT_COLDSTORAGESTACK_HPP
